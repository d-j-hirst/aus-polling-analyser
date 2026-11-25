#include "ForecastSpecificationIO.h"
#include "ForecastSpecificationExport.h"
#include "ForecastSpecificationProjectAdapter.h"
#include "General.h"
#include "LivePartyMapping.h"
#include "PollingProject.h"
#include "WorkspacePaths.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <string>

namespace {
	bool checkProjectDrawCounts(ForecastSpecification const& package, WorkspacePaths const& paths)
	{
		// The command line starts from the package. The GUI starts from its
		// saved simulation, both before and after another copy saves a replay.
		auto saved = ForecastSpecificationProjectAdapter::construct(package, paths);
		if (!saved.valid()) return false;
		auto const folder = paths.root() / "cli-build" / "project-draw-count-test";
		auto& project = *saved.project;
		auto const initial = exportForecastSpecification(project, folder);
		if (!initial.valid()) return false;
		auto const index = project.simulations().count() - 1;
		auto& simulation = project.simulations().access(project.simulations().indexToId(index));
		auto const packageDraws = package.simulations.at(index).numIterations;
		if (simulation.getSettings().numIterations != packageDraws) return false;
		auto settings = simulation.getSettings();
		settings.numIterations = packageDraws + 40000;
		simulation.replaceSettings(settings);

		auto const autosave = exportForecastSpecification(project, folder,
			SimulationDrawDefaults::KeepPackageDefaults);
		auto const reloaded = loadForecastSpecification(folder / "forecast.json", paths.root());
		if (!autosave.valid() || !reloaded.valid() ||
			reloaded.specification.simulations.at(index).numIterations != packageDraws) return false;
		auto const overlay = ForecastSpecificationProjectAdapter::apply(project, reloaded.specification);
		return overlay.valid() && simulation.getSettings().numIterations == settings.numIterations;
	}

	bool checkSaPartyMigration(ForecastSpecification const& current, WorkspacePaths const& paths)
	{
		auto old = current;
		old.parties.insert(old.parties.begin() + 3, {
			{3, "sa-best", "SA Best", "SAB"}, {4, "democrats", "Democrats", "DEM"},
			{5, "family-first", "Family First", "FF"}});
		for (int index = 0; index < int(old.parties.size()); ++index)
			old.parties[index].index = index;
		auto saved = ForecastSpecificationProjectAdapter::construct(old, paths);
		if (!saved.valid()) return false;
		auto& project = *saved.project;
		std::vector<std::vector<std::string>> seatLists;
		for (auto const& [id, district] : project.seats()) {
			auto& codes = seatLists.emplace_back();
			for (auto const& entry : district.runningParties) codes.push_back(entry.substr(0, entry.find('*')));
		}
		if (LivePartyMapping::partiesMissingFromSeatLists(project.parties(), seatLists) != std::vector<int>{3,4,5} ||
			!LivePartyMapping::partiesMissingFromSeatLists(project.parties(), {{},{}}).empty()) return false;
		Poll poll;
		poll.primary[3] = 2;
		poll.primary[4] = 1;
		poll.primary[5] = 3;
		poll.primary[6] = 5;
		poll.primary[7] = 7;
		poll.primary[PartyCollection::MaxParties] = 4;
		project.polls().add(poll);
		poll.primary[PartyCollection::MaxParties] = Poll::NullValue;
		project.polls().add(poll);

		auto& seat = project.seats().accessByName("Flinders").second;
		seat.livePartyOne = 3;
		auto blocked = ForecastSpecificationProjectAdapter::apply(project, current);
		if (blocked.valid() || blocked.errorMessage().find("Flinders") == std::string::npos ||
			project.parties().count() != 8) return false;
		seat.livePartyOne = 7;
		auto pollMatches = [](Poll const& result, float others) {
			return result.primary[3] == 5 && result.primary[4] == 7 &&
				result.primary[5] == Poll::NullValue && result.primary[6] == Poll::NullValue &&
				result.primary[7] == Poll::NullValue && result.primary[PartyCollection::MaxParties] == others;
		};
		// Reopening or reapplying the package must never add the same poll shares twice.
		for (int application = 0; application < 2; ++application) {
			auto applied = ForecastSpecificationProjectAdapter::apply(project, current);
			if (!applied.valid() || project.parties().count() != 5 || seat.livePartyOne != 4 ||
				!LivePartyMapping::partiesMissingFromSeatLists(project.parties(), seatLists).empty() ||
				project.parties().indexByShortCode("IND") != 3 || project.parties().indexByShortCode("ONP") != 4 ||
				!pollMatches(project.polls().viewByIndex(0), 10) ||
				!pollMatches(project.polls().viewByIndex(1), Poll::NullValue)) return false;
		}
		// Import the original poll-column layout into the new project and require
		// the same totals as the migrated saved polls, including a missing residual.
		auto folder = paths.root() / "cli-build" / "sa-poll-import";
		std::filesystem::create_directories(folder);
		auto file = folder / "polls.csv";
		std::ofstream(file) <<
			"MidDate,Firm,Brand,@TPP,ALP FP,SAB FP,DEM FP,FF FP,IND FP,ONP FP,OTH FP\n"
			"2026-03-01,Example,,50,40,2,1,3,5,7,4\n"
			"2026-03-02,Example,,50,40,2,1,3,5,7,#N/A\n";
		project.pollsters().add(Pollster("Example", 0));
		return project.polls().collectPolls([&](std::string, std::string) { return file.string(); }) &&
			project.polls().count() == 2 && pollMatches(project.polls().viewByIndex(0), 10) &&
			pollMatches(project.polls().viewByIndex(1), Poll::NullValue);
	}

	bool checkIndependentPreferenceImport(std::filesystem::path const& root)
	{
		// Exercise the real seat importer/exporter in an isolated workspace,
		// including legacy omission, both list separators and a misspelled code.
		auto const fixture = root / "cli-build" / "independent-preference-import";
		std::filesystem::create_directories(fixture / "analysis" / "seats");
		auto const seatsPath = fixture / "analysis" / "seats" / "2030fed.txt";
		std::ofstream(fixture / "config.cfg") << "iSimulationThreads=1\n";
		auto writeSeats = [&](std::string const& setting) {
			std::ofstream(seatsPath) <<
				"#Default\nsIncumbent=ALP\nsChallenger=LIB\nsRegion=Area\n"
				"#Exception\nsIncumbent=LIB\nsChallenger=CA\nsRegion=Area\n" <<
				"sIndependentLikePreferences=" << setting << '\n';
		};
		ForecastSpecification specification;
		specification.id = "preference-test";
		specification.electionName = "Fictional election";
		specification.electionCode = "2030fed";
		specification.dataSources.seats = "analysis/seats/2030fed.txt";
		specification.partySettings = {"labor", "liberal"};
		specification.parties = {{0,"labor","Labor","ALP"},
			{1,"liberal","Liberal","LIB"}, {2,"independent","Independent","IND"},
			{3,"centrist","Centrist Party","CA"}};
		specification.partyOfficialCodes = {{"labor","ALP"}, {"liberal","LIB"},
			{"independent","IND"}, {"centrist","CA"}};
		specification.regions = {{0,"area","Area",10000,"Area",50}};
		specification.models = {{"model","Model","2030fed",{{"ALP",0,0},{"LIB",0,0}}}};
		specification.projections = {{"projection","Projection","model",10,
			*Date::parseIso("2030-01-01"),{}}};
		specification.simulations = {{"simulation","Simulation","projection",10}};
		for (auto const& setting : {"CA,IND", "CA;IND", ""}) {
			writeSeats(setting);
			auto result = ForecastSpecificationProjectAdapter::construct(
				specification, WorkspacePaths(fixture));
			if (!result.valid()) {
				std::cerr << result.errorMessage() << '\n';
				return false;
			}
			auto& seats = result.project->seats();
			std::vector<std::string> const expected = std::string(setting).empty() ?
				std::vector<std::string>{} : std::vector<std::string>{"CA", "IND"};
			if (!seats.viewByIndex(0).independentLikePreferences.empty() ||
				seats.viewByIndex(1).independentLikePreferences != expected) {
				std::cerr << "Imported preference list differs for " << setting << '\n';
				return false;
			}
			seats.exportInfo();
			seats.importInfo();
			if (seats.viewByIndex(1).independentLikePreferences != expected) {
				std::cerr << "Preference list changed on export for " << setting << '\n';
				return false;
			}
		}
		writeSeats("MISSPELLED");
		auto invalid = ForecastSpecificationProjectAdapter::construct(
			specification, WorkspacePaths(fixture));
		return !invalid.valid() && invalid.errorMessage().find("MISSPELLED") != std::string::npos;
	}

	bool checkNationalsLookup(PollingProject& owner)
	{
		// Feed aliases remain useful when a project groups the Coalition together.
		// The separate-Nationals machinery must distinguish that setup from a
		// project with two independently configured Coalition parties.
		PartyCollection grouped(owner);
		Party labor;
		labor.officialCodes = { "ALP" };
		grouped.add(labor);
		if (grouped.separateNationalsIndex() != PartyCollection::InvalidIndex)
			return false;
		Party coalition;
		coalition.officialCodes = { "LIB", "NAT", "LNP" };
		grouped.add(coalition);
		if (grouped.indexByShortCode("NAT") != 1 ||
			grouped.indexByShortCode("LIB") != 1 ||
			grouped.separateNationalsIndex() != PartyCollection::InvalidIndex)
			return false;

		Party liberal;
		liberal.officialCodes = { "LIB", "LP", "LNP" };
		grouped.replace(grouped.indexToId(1), liberal);
		Party green;
		green.officialCodes = { "GRN" };
		grouped.add(green);
		Party national;
		national.officialCodes = { "NAT", "NP" };
		grouped.add(national);
		if (grouped.separateNationalsIndex() != 3 ||
			grouped.indexByShortCode("LIB") != 1 ||
			grouped.indexByShortCode("NP") != 3)
			return false;

		// Neither of the first two parties is an additional Coalition partner,
		// even if a custom project calls its first major party NAT.
		PartyCollection majorNationals(owner);
		majorNationals.add(national);
		majorNationals.add(liberal);
		return majorNationals.separateNationalsIndex() ==
			PartyCollection::InvalidIndex;
	}

	bool checkVicPartyMigration(ForecastSpecification const& current, WorkspacePaths const& paths)
	{
		auto old = current;
		old.parties.insert(old.parties.begin() + 4, {
			{4, "one-nation", "One Nation", "ONP"}, {5, "uap", "United Australia", "UAP"},
			{6, "democrats", "Democrats", "DEM"}});
		for (int i = 0; i < int(old.parties.size()); ++i) old.parties[i].index = i;
		auto saved = ForecastSpecificationProjectAdapter::construct(old, paths);
		if (!saved.valid()) return false;
		auto& project = *saved.project;
		auto& seat = project.seats().accessByName("Euroa").second;
		seat.livePartyOne = 7;
		Poll poll;
		poll.primary[4] = 2;
		poll.primary[5] = 1;
		poll.primary[6] = 3;
		poll.primary[7] = 8;
		poll.primary[PartyCollection::MaxParties] = 4;
		project.polls().add(poll);
		for (int pass = 0; pass < 2; ++pass) {
			auto result = ForecastSpecificationProjectAdapter::apply(project, current);
			auto const& shares = project.polls().viewByIndex(0).primary;
			if (!result.valid() || seat.livePartyOne != 4 || project.parties().indexByShortCode("NAT") != 4 ||
				shares[4] != 8 || shares[7] != Poll::NullValue || shares[PartyCollection::MaxParties] != 10) return false;
		}
		auto folder = paths.root() / "cli-build" / "vic-poll-import";
		std::filesystem::create_directories(folder);
		auto file = folder / "polls.csv";
		std::ofstream(file) << "MidDate,Firm,Brand,@TPP,ALP FP,ONP FP,UAP FP,DEM FP,NAT FP,OTH FP\n"
			"2022-11-20,Example,,50,40,2,1,3,8,4\n";
		project.pollsters().add(Pollster("Example", 0));
		if (!project.polls().collectPolls([&](std::string, std::string) { return file.string(); })) return false;
		auto const& imported = project.polls().viewByIndex(0).primary;
		return imported[4] == 8 && imported[PartyCollection::MaxParties] == 10;
	}

	bool checkVictorianCoalition(PollingProject const& project)
	{
		// These are configuration checks, using the known 2022 candidate roster.
		// Both-party contests must remain possible, while Nationals-only seats
		// must not acquire a fictional Liberal candidate.
		auto const& parties = project.parties();
		auto const national = parties.separateNationalsIndex();
		if (parties.count() != 5 || national != 4 ||
			parties.indexByShortCode("LIB") != 1 ||
			parties.viewByIndex(national).relationType !=
				Party::RelationType::Coalition ||
			parties.viewByIndex(national).relationTarget != parties.indexToId(1))
			return false;
		std::vector<std::string> const nationalOnly = {
			"Gippsland East", "Gippsland South", "Lowan", "Murray Plains",
			"Ovens Valley" };
		std::vector<std::string> const both = {
			"Bass", "Euroa", "Mildura", "Morwell", "Shepparton" };
		for (auto const& [id, seat] : project.seats()) {
			static_cast<void>(id);
			if (seat.runningParties.empty()) return false;
			// Every other seat has a known Liberal candidate and no Nationals
			// candidate. An omitted list would leave candidacy undecided.
			if (!contains(nationalOnly, seat.name) && !contains(both, seat.name) &&
				(contains(seat.runningParties, std::string("NAT")) ||
					!contains(seat.runningParties, std::string("LNP")))) return false;
			if (contains(nationalOnly, seat.name)) {
				if (seat.incumbent != parties.indexToId(national) ||
					!contains(seat.runningParties, std::string("NAT")) ||
					contains(seat.runningParties, std::string("LNP"))) return false;
			}
			if (contains(both, seat.name) &&
				(!contains(seat.runningParties, std::string("NAT")) ||
					!contains(seat.runningParties, std::string("LNP")))) return false;
			if (seat.name == "Euroa" &&
				seat.incumbent != parties.indexToId(national)) return false;
			if (seat.name == "Mildura" &&
				(seat.candidateNames.at("Jade Benham") != "NAT" ||
					seat.candidateNames.at("Paul Matheson") != "LNP" ||
					!seat.nationalsCoalitionShare ||
					std::abs(*seat.nationalsCoalitionShare -
						12808.f / (12808.f + 3686.f)) > 0.000001f)) return false;
		}
		return true;
	}
}

int main(int argc, char const* argv[])
{
	if (argc < 3) {
		std::cerr << "Usage: adapter-tests <workspace> <forecast.json>...\n";
		return 2;
	}

	WorkspacePaths const paths(argv[1]);
	bool valid = true;
	if (!checkIndependentPreferenceImport(paths.root())) {
		std::cerr << "Independent-like preference settings failed import/export checks\n";
		valid = false;
	}
	for (int argument = 2; argument < argc; ++argument) {
		auto loaded = loadForecastSpecification(argv[argument], paths.root());
		if (!loaded.valid()) {
			for (auto const& diagnostic : loaded.diagnostics) {
				std::cerr << argv[argument] << ": " << diagnostic.location <<
					": " << diagnostic.message << '\n';
			}
			valid = false;
			continue;
		}

		auto constructed = ForecastSpecificationProjectAdapter::construct(
			loaded.specification, paths);
		if (!constructed.valid()) {
			std::cerr << argv[argument] << ": " <<
				constructed.errorMessage() << '\n';
			valid = false;
			continue;
		}

		auto const& specification = loaded.specification;
		if (specification.electionCode == "2026vic" && !checkProjectDrawCounts(specification, paths)) {
			std::cerr << "Saving or opening a project mixed up local and portable iteration counts\n";
			valid = false;
		}
		auto const& project = *constructed.project;
		if (specification.electionCode == "2026sa" && !checkSaPartyMigration(specification, paths)) {
			std::cerr << "2026sa party migration or historical poll import failed\n";
			valid = false;
		}
		// The exception is local to Sharkie's post-2016 campaigns. It must not
		// become a CA-wide default or retrospectively change NXT in 2016.
		for (auto const& [seatId, seat] : project.seats()) {
			static_cast<void>(seatId);
			bool const sharkieException = seat.name == "Mayo" &&
				(specification.electionCode == "2019fed" ||
				 specification.electionCode == "2022fed" ||
				 specification.electionCode == "2025fed" ||
				 specification.electionCode == "2028fed");
			std::vector<std::string> const expected = sharkieException ?
				std::vector<std::string>{"CA"} : std::vector<std::string>{};
			if (seat.independentLikePreferences != expected) {
				std::cerr << specification.electionCode << " / " << seat.name <<
					": unexpected independent-like preference configuration\n";
				valid = false;
			}
		}
		if (!checkNationalsLookup(*constructed.project)) {
			std::cerr << "Combined and separate Nationals lookups disagree\n";
			valid = false;
		}
		if (specification.electionCode == "2022vic" &&
			(!checkVictorianCoalition(project) || !checkVicPartyMigration(specification, paths))) {
			std::cerr << "2022vic Coalition configuration was not imported correctly\n";
			valid = false;
		}
		if (!project.isValid() ||
			project.seats().count() == 0 ||
			constructed.runtimeIds.parties.size() !=
				specification.parties.size() ||
			constructed.runtimeIds.regions.size() !=
				specification.regions.size() ||
			constructed.runtimeIds.models.size() !=
				specification.models.size() ||
			constructed.runtimeIds.projections.size() !=
				specification.projections.size() ||
			constructed.runtimeIds.simulations.size() !=
				specification.simulations.size() ||
			project.seats().importSourcePath() !=
				paths.resolve(specification.dataSources.seats)) {
			std::cerr << argv[argument] <<
				": constructed project does not match the specification\n";
			valid = false;
			continue;
		}

		for (auto const& configured : specification.models) {
			auto const modelId =
				constructed.runtimeIds.models.at(configured.id);
			if (project.models().view(modelId).getName() != configured.name) {
				std::cerr << argv[argument] <<
					": model ID mapping is incorrect for " <<
					configured.id << '\n';
				valid = false;
			}
		}
		for (auto const& configured : specification.projections) {
			auto const projectionId =
				constructed.runtimeIds.projections.at(configured.id);
			auto const& settings =
				project.projections().view(projectionId).getSettings();
			if (settings.name != configured.name ||
				settings.baseModel != constructed.runtimeIds.models.at(
					configured.baseModelId)) {
				std::cerr << argv[argument] <<
					": projection ID mapping is incorrect for " <<
					configured.id << '\n';
				valid = false;
			}
		}
		for (auto const& configured : specification.simulations) {
			auto const simulationId =
				constructed.runtimeIds.simulations.at(configured.id);
			auto const& settings =
				project.simulations().view(simulationId).getSettings();
			if (settings.name != configured.name ||
				settings.baseProjection !=
					constructed.runtimeIds.projections.at(
						configured.baseProjectionId)) {
				std::cerr << argv[argument] <<
					": simulation ID mapping is incorrect for " <<
					configured.id << '\n';
				valid = false;
			}
		}

		if (specification.electionCode == "2026vic") {
			auto seatByName = [&](std::string const& name) -> Seat const* {
				for (auto const& [id, seat] : project.seats()) {
					static_cast<void>(id);
					if (seat.name == name) return &seat;
				}
				return nullptr;
			};
			auto const* bass = seatByName("Bass");
			auto const* ripon = seatByName("Ripon");
			if (!bass || !ripon) {
				std::cerr << argv[argument] <<
					": Coalition candidacy test seats are missing\n";
				valid = false;
				continue;
			}
			if (bass->coalitionCandidates.at("LNP") != 1.0f ||
				bass->coalitionCandidates.at("NAT") != 0.2f ||
				!bass->nationalsCoalitionShare ||
				std::abs(*bass->nationalsCoalitionShare -
					0.3064516129f) > 0.000001f ||
				ripon->coalitionCandidates.at("LNP") != 1.0f ||
				ripon->coalitionCandidates.at("NAT") != 1.0f ||
				!ripon->nationalsCoalitionShare ||
				std::abs(*ripon->nationalsCoalitionShare - 0.32f) >
					0.000001f) {
				std::cerr << argv[argument] <<
					": Coalition seat settings were not imported correctly\n";
				valid = false;
			}
		}
		if (specification.electionCode == "2028fed") {
			auto seatByName = [&](std::string const& name) -> Seat const* {
				for (auto const& [id, seat] : project.seats()) {
					static_cast<void>(id);
					if (seat.name == name) return &seat;
				}
				return nullptr;
			};
			for (auto const& name : { "Bean", "Canberra", "Fremantle" }) {
				auto const* seat = seatByName(name);
				if (!seat || !seat->previousIndRunning ||
					seat->confirmedProminentIndependent) {
					std::cerr << argv[argument] <<
						": returning-independent setting was not imported for " <<
						name << '\n';
					valid = false;
				}
			}
			auto const* grayndler = seatByName("Grayndler");
			if (!grayndler || grayndler->previousIndRunning ||
				!grayndler->confirmedProminentIndependent) {
				std::cerr << argv[argument] <<
					": emerging-independent setting was not retained for Grayndler\n";
				valid = false;
			}
		}
	}
	return valid ? 0 : 1;
}
