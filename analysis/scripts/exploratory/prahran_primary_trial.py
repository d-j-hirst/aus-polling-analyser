"""Prepare a private Victorian Nowcast experiment from the checkout's portable CLI.

Run this script with the repository's Windows analysis Python, then run build.cmd
and run.cmd in tmp/prahran-fp-trial. Each invocation of the executable prepares
the model and projection once, using the existing poll analysis outputs. Nothing
is uploaded. The working forecast, GUI, source files and live histories are
never opened for writing. Source anchors deliberately fail if the relevant
simulation paths change, so the trial cannot silently measure the wrong stage.
"""
import csv
import json
import math
from pathlib import Path
import shutil
from statistics import mean, median
import sys


ROOT = Path(__file__).resolve().parents[3]
LAB = ROOT / "tmp/prahran-fp-trial"
ARMS = ["current", "no-odds", "taper", "quarter", "half", "three-quarters"]
DRAWS = 6000


def edit(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f"Expected one source anchor: {old!r}")
    return source.replace(old, new)


def source(name):
    return (ROOT / name).read_text(encoding="utf-8-sig")


def prepare_iteration():
    code = '#include "Trial.h"\n' + source("SimulationIteration.cpp")
    # Record only the ultimately accepted attempt. A thread processes one
    # election at a time and overwrites these values on every retry.
    code = edit(code, "using Mp = Simulation::MajorParty;", """using Mp = Simulation::MajorParty;
static thread_local double rawGreen, normalisedGreen, initialGreen, initialLabor, taperedGreen, taperedLabor;""")
    anchor = "voteShare = regularVoteShare;\n\t}\n}\n\n// Combine the seat's"
    code = edit(code, anchor, anchor.replace("voteShare = regularVoteShare;", """voteShare = regularVoteShare;
            if (trialRecording && seat.name == "Prahran" && partyIndex == run.grnPartyIndex)
                rawGreen = regularVoteShare;"""))
    anchor = "determineSeatInitialFp(seatIndex);"
    code = edit(code, anchor, anchor + """
        if (trialRecording && run.seatLookup.seat(seatIndex).name == "Prahran")
            normalisedGreen = getAt(seatFpVoteShare[seatIndex], run.grnPartyIndex, 0.);""")
    original = "run.oddsFinalMeans.at({seatIndex, partyIndex}), OddsWeight);"
    code = edit(code, original, original.replace("OddsWeight", """OddsWeight *
            ((seat.name == "Prahran" && partyIndex == run.grnPartyIndex) ?
                trialFraction[trialArm] : 1.0)"""))
    # Use the first compatible district primaries so that both statewide FP
    # reconciliation and the ranked exclusion count see the amended votes.
    # Recalibrating the odds with this same rule lets the market push back.
    anchor = '\tif (hasInvalidValues("After corrections"))'
    code = edit(code, anchor, """
    auto captureTrial = [&](double& g, double& a) {
        if (!trialRecording) return;
        for (int s=0; s<project.seats().count(); ++s)
            if (run.seatLookup.seat(s).name == "Prahran") {
                g = getAt(seatFpVoteShare[s], run.grnPartyIndex, 0.);
                a = getAt(seatFpVoteShare[s], Mp::One, 0.);
            }
    };
    captureTrial(initialGreen, initialLabor);
    if (trialArm == 2) {
        for (int s = 0; s < project.seats().count(); ++s) {
            auto const& past = run.pastSeatResults[s].fpVotePercent;
            auto& fp = seatFpVoteShare[s];
            auto green = run.grnPartyIndex;
            if (!past.contains(green) || !past.contains(Mp::One) ||
                !fp.contains(green) || !fp.contains(Mp::One) || !fp.contains(Mp::Two)) continue;
            double start = past.at(green);
            double gain = fp.at(green) - start;
            double threshold = std::min(10., .5 * past.at(Mp::One));
            double labor = fp.at(Mp::One);
            if (start <= 0 || gain <= 0 || threshold <= 0 || labor >= threshold) continue;
            // Use the Greens transfer rate from this draw wherever available.
            double rate = getAt(overallPreferenceFlow, green, 82.) / 100.;
            for (auto const& [party, share] : preferenceSourceRates[s])
                if (party == green) rate = share;
            if (rate <= 0 || rate > 1) continue;
            double laborBeforeGain = labor + rate * gain;
            double available = std::min(threshold, laborBeforeGain);
            double costBelowThreshold = available - labor;
            double taperedLabor = available * std::exp(-costBelowThreshold / threshold);
            double returned = std::clamp((taperedLabor - labor) / rate, 0., gain);
            fp[green] -= returned;
            fp[Mp::One] += rate * returned;
            fp[Mp::Two] += (1. - rate) * returned;
        }
    }
    captureTrial(taperedGreen, taperedLabor);
""" + anchor)
    anchor = "\trecordIterationResults();"
    code = edit(code, anchor, """
    if (trialRecording) {
        for (int s = 0; s < project.seats().count(); ++s) {
            if (run.seatLookup.seat(s).name != "Prahran") continue;
            auto const& fp = seatFpVoteShare[s];
            auto const pair = seatTcpVoteShare[s].first;
            trialDraws << iterationIndex << ',' << retryCount << ',' << fp.at(0) << ','
                << fp.at(1) << ',' << fp.at(run.grnPartyIndex) << ',' << pair.first << ','
                << pair.second << ',' << seatWinner[s] << ',' << rawGreen << ',' << normalisedGreen
                << ',' << initialGreen << ',' << initialLabor << ',' << taperedGreen << ',' << taperedLabor << '\\n';
        }
    }
""" + anchor)
    return code


def prepare_run():
    code = '#include "Trial.h"\n' + source("SimulationRun.cpp")
    code = edit(code, "nowcastDate = Date::todayLocal();", "nowcastDate = Date::fromYmd(2026, 11, 25).value();")
    anchor = "if (!std::isfinite(odds) || odds <= 0.0f)"
    code = edit(code, anchor, 'if (trialArm == 1 && seat.name == "Prahran" && partyCode == "GRN") continue;\n\t\t\t' + anchor)
    anchor = 'if (!runIterations(*this, cycleIterations, 0, "simulation", feedback)) return false;'
    code = edit(code, anchor, """
    trialDraws.open(std::string(trialNames[trialArm]) + "-draws.csv");
    trialDraws << "draw,retry,labor,liberal,greens,first,second,winner,raw_green,normalised_green,initial_green,initial_labor,tapered_green,tapered_labor\\n" << std::setprecision(12);
    trialRecording = true;
    """ + anchor + """
    trialRecording = false;
    trialDraws.close();""")
    return code


def prepare_cli():
    code = '#include "Trial.h"\n#include "Simulation.h"\n' + source("PollingCli.cpp")
    anchor = "std::cout <<\n\t\t\t\t\tformatCoreReportSummary("
    return edit(code, anchor, """
                auto const& r = simulation.getLatestReport();
                std::ofstream out(std::string(trialNames[trialArm]) + "-seats.csv");
                out << "seat,party,mean_fp,win\\n" << std::setprecision(12);
                for (size_t s=0; s<r.seatName.size(); ++s)
                    for (auto [party, share] : r.seatPartyMeanFpShare[s])
                        out << r.seatName[s] << ',' << party << ',' << share << ','
                            << getAt(r.seatPartyWinPercent[s], party, 0.) << '\\n';
                std::cout << trialNames[trialArm++] << " finished.\\n";
                """ + anchor)


def main():
    LAB.mkdir(parents=True, exist_ok=True)
    (LAB / "obj").mkdir(exist_ok=True)
    (LAB / "Trial.h").write_text("""#pragma once
#include <fstream>
#include <iomanip>
inline int trialArm = 0;
inline bool trialRecording = false;
inline std::ofstream trialDraws;
inline constexpr double trialFraction[]{1.,0.,1.,.25,.5,.75};
inline constexpr const char* trialNames[]{""" + ",".join(map(json.dumps, ARMS)) + "};\n")
    (LAB / "SimulationIteration.cpp").write_text(prepare_iteration())
    (LAB / "SimulationRun.cpp").write_text(prepare_run())
    (LAB / "PollingCli.cpp").write_text(prepare_cli())
    config = source("Config.cpp").replace("std::stoi(values[1]);", "std::min(10, std::stoi(values[1]));")
    (LAB / "Config.cpp").write_text(config)
    sources = [x for x in source("cli-sources.txt").splitlines() if x.endswith(".cpp")] + ["PollingCli.cpp"]
    paths = [LAB / x if x in ("SimulationIteration.cpp", "SimulationRun.cpp", "PollingCli.cpp", "Config.cpp") else ROOT / x for x in sources]
    (LAB / "sources.rsp").write_text("\n".join(f'"{p}"' for p in paths))
    manifest = json.loads((ROOT / "forecasts/2026vic/forecast.json").read_bytes())
    for filename in manifest["files"].values():
        shutil.copyfile(ROOT / "forecasts/2026vic" / filename, LAB / filename)
    for sim in manifest["simulations"]:
        sim["num_iterations"] = DRAWS
    (LAB / "forecast.json").write_text(json.dumps(manifest))
    common = '@echo off\npushd "%~dp0"\n'
    (LAB / "build.cmd").write_text(common + r'''call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /O2 /EHsc /MT /MP6 /DNDEBUG /W3 /wd4244 /wd4267 /I"..\.." /Fo"obj\\" /Fetrial.exe @sources.rsp
exit /b %errorlevel%
''')
    macro = ";".join(["run-model", "run-projection"] + ["run-simulation:nowcast-simulation"] * len(ARMS))
    (LAB / "run.cmd").write_text(common + f'trial.exe --no-log --forecast forecast.json --macro "{macro}"\nexit /b %errorlevel%\n')


def read_draws(arm):
    with (LAB / f"{arm}-draws.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    draws = {int(r["draw"]): {k: float(v) for k, v in r.items()} for r in rows}
    assert len(rows) == DRAWS and set(draws) == set(range(DRAWS))
    for row in draws.values():
        assert row["winner"] in (row["first"], row["second"])
        assert all(math.isfinite(v) for v in row.values())
    return draws


def summarise():
    draws = {arm: read_draws(arm) for arm in ARMS}
    target = 94 / 1.405  # Same conversion used by calculateImpliedChance for this price.
    neutral = draws["no-odds"]

    def summary(arm, keys):
        rows = [draws[arm][k] for k in keys]
        alps = [r["labor"] for r in rows]
        # The two historic major primaries exceed 20%, so both thresholds are 10%.
        # Charge only damage relative to the same neutral scenario. Improving
        # another major's primary cannot cancel the first major's extra shortfall.
        loss = mean(sum(max(0, max(0, 10 - draws[arm][k][p]) - max(0, 10 - neutral[k][p]))
                        for p in ("labor", "liberal")) for k in keys)
        result = dict(arm=arm, labor_mean=mean(alps), labor_median=median(alps),
                      green_mean=mean(r["greens"] for r in rows), green_median=median(r["greens"] for r in rows),
                      added_shortfall=loss)
        for threshold in (2, 5, 10):
            result[f"labor_below_{threshold}"] = 100 * mean(a < threshold for a in alps)
        for p, name in [(0, "labor"), (1, "liberal"), (2, "green")]:
            result[f"{name}_win"] = 100 * mean(r["winner"] == p for r in rows)
        for a, b, name in [(1, 2, "lib_grn"), (0, 1, "alp_lib"), (0, 2, "alp_grn")]:
            result[name] = 100 * mean({r["first"], r["second"]} == {a, b} for r in rows)
        result["market_error"] = abs(target - result["green_win"])
        return result

    full = [summary(arm, range(DRAWS)) for arm in ARMS]
    with (LAB / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=full[0])
        writer.writeheader()
        writer.writerows(full)
    for r in full:
        print(" ".join(f"{k}={v:.2f}" if isinstance(v, float) else str(v) for k, v in r.items()))
    # Select an odds strength on one half and report on disjoint draws. The
    # exchange rate is a modelling choice: two win-chance points of fit per
    # extra point of major-primary shortfall. Show alternatives either side.
    grid = ["no-odds", "quarter", "half", "three-quarters", "current"]
    fit = [summary(arm, range(0, DRAWS, 2)) for arm in grid]
    selections = []
    for penalty in (0, 1, 2, 4):
        chosen = min(fit, key=lambda r: r["market_error"] + penalty * r["added_shortfall"])
        evaluation = summary(chosen["arm"], range(1, DRAWS, 2))
        selections.append(dict(penalty=penalty, **evaluation))
        print("Penalty", penalty, "held-out Greens win chance", evaluation["arm"], evaluation["green_win"])
    with (LAB / "penalty-selection.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=selections[0])
        writer.writeheader()
        writer.writerows(selections)
    print("Different retry keys vs neutral", {
        arm: sum(row["retry"] != neutral[k]["retry"] for k, row in runs.items())
        for arm, runs in draws.items()})


if __name__ == "__main__":
    if sys.argv[1:] == ["--summarise"]:
        summarise()
    elif len(sys.argv) == 1:
        main()
    else:
        raise SystemExit("Use no arguments to prepare the trial, or --summarise after run.cmd.")
