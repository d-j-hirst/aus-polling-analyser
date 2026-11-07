// Verify that final AEC district totals replace estimates without treating
// progressive TPP placeholders/counts as completion. No forecast is calculated.
#include "ElectionData.h"
#include "LiveInputAdapter.h"
#include "LiveTurnout.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
Results2::Election fixture() {
    Results2::Election e("fictional");
    e.candidates.emplace(11,Results2::Candidate{11,"Labor",1});
    e.candidates.emplace(12,Results2::Candidate{12,"Liberal",2});
    e.candidates.emplace(13,Results2::Candidate{13,"Independent",-1});
    e.parties.emplace(1,Results2::Party{1,"Labor","ALP"});
    e.parties.emplace(2,Results2::Party{2,"Liberal","LP"});
    return e;
}

std::string source(std::string phase, bool declared, bool classic, int tppTotal=1000) {
    auto candidate = [](int id,int votes,bool elected=false) {
        auto n=std::to_string(votes);
        return "<Candidate><eml:CandidateIdentifier Id='"+std::to_string(id)+
            "'/>"+std::string(elected?"<Elected>true</Elected>":"")+"<Votes>"+n+"</Votes><VotesByType><Votes Type='Ordinary'>"+n+
            "</Votes></VotesByType></Candidate>";
    };
    return "<MediaFeed Created='2030-01-01T20:00:00'><Results Phase='"+phase+
        "'><eml:EventIdentifier Id='99'><eml:EventName>Fictional</eml:EventName>"
        "</eml:EventIdentifier><Election><House><Contests><Contest"+
        std::string(declared?" Declared='2030-01-01T19:00:00'":"")+
        "><eml:ContestIdentifier Id='1'><eml:ContestName>Example</eml:ContestName>"
        "</eml:ContestIdentifier><Enrolment>1500</Enrolment><FirstPreferences>"+
        candidate(11,350)+candidate(12,400)+candidate(13,250)+
        "</FirstPreferences><TwoCandidatePreferred>"+
        candidate(classic?11:13,600,true)+candidate(12,400)+
        "</TwoCandidatePreferred><TwoPartyPreferred><Coalition>"
        "<CoalitionIdentifier Id='1'/><Votes>"+std::to_string(tppTotal/2)+
        "</Votes></Coalition><Coalition><CoalitionIdentifier Id='2'/><Votes>"+
        std::to_string(tppTotal-tppTotal/2)+
        "</Votes></Coalition></TwoPartyPreferred><PollingPlaces/>"
        "</Contest></Contests></House></Election></Results></MediaFeed>";
}

void update(Results2::Election& e,std::string const& xml) {
    tinyxml2::XMLDocument doc;
    assert(doc.Parse(xml.c_str())==tinyxml2::XML_SUCCESS);
    e.updateLive(doc,Results2::Election::Format::AEC);
}

void checkFinality() {
    auto e=fixture();
    for (bool declared : {false,true}) for (bool classic : {false,true}) {
        update(e,source("ElectionNight",declared,classic,0));
        assert(!e.seats.at(1).finalResult && !e.seats.at(1).fpFinalised);
    }
    // Positive classic TPP remains progressive even after a winner declaration.
    update(e,source("PostElection",true,true));
    assert(!e.seats.at(1).finalResult);
    auto countryLabor=fixture();countryLabor.parties.at(1).shortCode="CLR";
    update(countryLabor,source("PostElection",true,true));
    assert(!countryLabor.seats.at(1).finalResult);
    update(countryLabor,source("FinalResults",true,true,0));
    assert(countryLabor.seats.at(1).finalResult->tpp.at(1)==600);
    update(e,source("PostElection",false,false));
    assert(!e.seats.at(1).finalResult);
    update(e,source("PostElection",true,false,900));
    assert(!e.seats.at(1).finalResult); // Incomplete notional TPP.
    update(e,source("PostElection",true,false));
    auto final=e.seats.at(1).finalResult;
    assert(final && final->basis=="AEC declared non-classic TPP");
    assert(final->tcp.at(13)==600 && final->fp.at(13)==250);
    assert(final->winner==13);
    assert(final->tpp.at(1)==500 && final->tpp.at(2)==500);
    // An earlier replay after a final source does not inherit its finality.
    update(e,source("ElectionNight",false,true));
    assert(!e.seats.at(1).finalResult && !e.seats.at(1).fpFinalised);
    for (bool classic : {false,true}) {
        update(e,source("FinalResults",false,classic));
        assert(e.seats.at(1).finalResult && e.seats.at(1).fpFinalised);
    }
    update(e,source("FinalResults",false,true,0));
    assert(e.seats.at(1).finalResult->tpp.at(1)==600);
    auto tie=source("FinalResults",true,false);
    auto start=tie.find("<TwoCandidatePreferred>"),end=tie.find("</TwoCandidatePreferred>",start);
    auto tcp=tie.substr(start,end-start);
    for (std::string const number : {"600","400"}) {
        std::size_t pos=0;
        while ((pos=tcp.find(number,pos))!=std::string::npos) {tcp.replace(pos,3,"500");pos+=3;}
    }
    tie.replace(start,end-start,tcp);
    update(e,tie);
    assert(e.seats.at(1).finalResult->tcp.at(13)==500 && e.seats.at(1).finalResult->winner==13);
    bool rejected=false;
    try {update(e,source("FinalResults",true,false,900));}
    catch(std::runtime_error const&){rejected=true;}
    assert(rejected);
    auto wrongRoster=fixture();wrongRoster.liveCandidateRosters[1]={11,12};
    rejected=false;
    try {update(wrongRoster,source("FinalResults",true,false));}
    catch(std::runtime_error const&){rejected=true;}
    assert(rejected);
}

void checkRecoverySeparation() {
    auto e=fixture(); update(e,source("FinalResults",true,false));
    LiveInputRecovery::Snapshot effective;
    effective.sourceTime="2030-01-01T20:00:00";
    effective.districts["Example"]={1500,false};
    LiveInputRecovery::Record record;
    record.district="Example";record.role="declaration";record.name="Postal";
    record.candidates={11,12,13};
    record.fp.votes=LiveInputRecovery::Counts{{11,10},{12,10},{13,10}};
    record.tcp.votes=LiveInputRecovery::Counts{{11,15},{12,15}}; // Obsolete pair.
    effective.records.push_back(record);
    LiveInputAdapter::apply(e,effective);
    assert(e.seats.at(1).fpFinalised && e.seats.at(1).finalResult->tcp.contains(13));
    assert(e.seats.at(1).totalVotesFp()==30); // Service breakdown remains inspectable.
    assert(e.reportedCountProgress->fpVotes==1000 && e.reportedCountProgress->tcpVotes==1000);
}

void checkFixedCountDraws() {
    TurnoutModelIO::Artifact artifact;
    auto& prior=artifact.prior;
    prior.election="fictional";prior.seats={"Example"};prior.subdivisions={"X"};
    prior.groups={"ordinary"};prior.enrolment={20000};
    prior.totals={{9000},{10000}};prior.counts={{9000},{10000}};
    TurnoutModel::Unit unit;unit.name=unit.category="Booth";unit.kind="ordinary";
    unit.weight=1;unit.counted=8000;prior.units={unit};
    auto counts=LiveTurnout::Prepared::prepare(artifact,prior.units,{0},1,{true},{},"2025-03-09T10:00:00");
    LiveTurnout::UnitComposition u;u.fp={{0,4000},{1,3000},{2,1000}};
    u.tpp={{0,4800},{1,3200}};u.tcp={{0,4800},{1,3200}};
    u.projectedFp={{0,4000},{1,3000},{2,1000}};
    u.projectedTpp={{0,4800},{1,3200}};u.projectedTcp=u.projectedTpp;
    // Official aggregates can differ from a recovered booth and final TCP can
    // differ from the old pair. No synthetic booth allocation is needed.
    LiveTurnout::SeatComposition s;s.name="Example";s.finalised=true;
    s.fp={{0,3900},{1,3000},{2,1000}};s.tpp={{0,4500},{1,3400}};s.tcp={{1,3200},{2,4700}};
    s.projectedFp={s.fp.begin(),s.fp.end()};s.projectedTpp={s.tpp.begin(),s.tpp.end()};
    s.projectedTcp={s.tcp.begin(),s.tcp.end()};
    auto ready=counts->withComposition({u},{s});
    assert(ready->revisionDiagnostic().at("reported_accounts")==0);
    for(int i=0;i<300;++i) {
        auto draw=ready->draw(i);auto observed=ready->scenarioCounts();
        ready->revise(i,draw,observed);
        assert(draw[0].fp==s.projectedFp && draw[0].tpp==s.projectedTpp && draw[0].tcp==s.projectedTcp);
        assert(observed.fp[0]==s.fp && observed.tpp[0]==s.tpp && observed.tcp[0]==s.tcp);
        assert(ready->vary(LiveData::CountKind::Fp,0,draw[0].fp,{{0,100}})==s.projectedFp);
    }
    // Final district summaries group unmodelled non-finalists into Others.
    // The retained booth account may still identify them separately. That is
    // valid final data, not a reason to reintroduce uncertain booth projections.
    u.fp={{0,4000},{1,3000},{7,1000}};
    u.projectedFp={{0,4000},{1,3000},{7,1000}};
    s.fp={{0,3900},{1,3000},{-1,1000}};
    s.projectedFp={s.fp.begin(),s.fp.end()};
    auto grouped=counts->withComposition({u},{s});
    for(int i=0;i<20;++i) {
        auto draw=grouped->draw(i);
        assert(draw[0].fp==s.projectedFp && !draw[0].fp.contains(7));
        assert(grouped->vary(LiveData::CountKind::Fp,0,draw[0].fp,{})==s.projectedFp);
    }
}

void checkPrivateFeeds(std::filesystem::path const& root) {
    // Optional local source checks: no downloaded data is required by the
    // synthetic regressions above, and no results are used to fit parameters.
    for (auto const& election : {"2013fed","2016fed","2019fed","2022fed","2025fed"}) {
        auto finalPath=root/"cli-build"/"federal-final-counts"/(std::string(election)+"-final.xml");
        auto preload=root/"downloads"/(std::string(election)+"-preload.xml");
        if (std::string(election)=="2013fed") preload=root/"downloads"/"2013fed-final.xml";
        if (std::string(election)=="2025fed") preload=root/"downloads"/"2025fed_preload.xml";
        if (!std::filesystem::exists(finalPath) || !std::filesystem::exists(preload)) continue;
        tinyxml2::XMLDocument before,after;
        assert(before.LoadFile(preload.string().c_str())==tinyxml2::XML_SUCCESS);
        assert(after.LoadFile(finalPath.string().c_str())==tinyxml2::XML_SUCCESS);
        auto electionData=Results2::Election::createAec(before,election);
        electionData.updateLive(after,Results2::Election::Format::AEC);
        long long fp=0,tcp=0,alp=0,coalition=0;
        for (auto const& [id,seat] : electionData.seats) {
            assert(seat.finalResult && seat.fpFinalised);
            for(auto const& [c,v]:seat.finalResult->fp) fp+=v;
            for(auto const& [c,v]:seat.finalResult->tcp) tcp+=v;
            alp+=seat.finalResult->tpp.at(1);coalition+=seat.finalResult->tpp.at(2);
        }
        assert(fp==tcp && fp==alp+coalition);
        if (std::string(election)=="2016fed") assert(std::abs(100.*alp/fp-49.64350387756505)<1e-10);
        std::cout<<election<<": "<<electionData.seats.size()<<" final districts, "<<fp<<" formal votes, ALP TPP "<<100.*alp/fp<<"%\n";
        if (std::string(election)=="2016fed") {
            // Allora retains its actual LNP/One Nation candidate identifiers
            // even when the simulator later uses a generic slot for One Nation.
            auto const& allora=electionData.booths.at(6430);
            assert(allora.tcpVotesCandidate.at(28059)==585);
            assert(allora.tcpVotesCandidate.at(29224)==244);
            auto const& maranoa=electionData.seats.at(allora.parentSeat);
            assert(maranoa.name=="Maranoa" && maranoa.finalResult);
            assert(maranoa.finalResult->tcp.at(28059)==59308);
            assert(maranoa.finalResult->tcp.at(29224)==30741);
            auto latePath=root/"cli-build"/"federal-final-counts"/"2016fed-late.xml";
            if (!std::filesystem::exists(latePath)) continue;
            tinyxml2::XMLDocument late;
            assert(late.LoadFile(latePath.string().c_str())==tinyxml2::XML_SUCCESS);
            // Replay an earlier source after the all-final source. Kennedy
            // already has a reconciled final non-classic account; Melbourne's
            // incomplete notional TPP must still remain modelled.
            electionData.updateLive(late,Results2::Election::Format::AEC);
            bool kennedy=false,melbourne=false;
            for (auto const& [id,seat] : electionData.seats) {
                (void)id;
                if (seat.name=="Kennedy") {
                    assert(seat.finalResult);
                    int total=0;for(auto const& [c,v]:seat.finalResult->fp)total+=v;
                    assert(total==86010);
                    kennedy=true;
                }
                if (seat.name=="Melbourne") {
                    assert(!seat.finalResult);
                    melbourne=true;
                }
            }
            assert(kennedy && melbourne);
            std::cout<<"2016 late source: Kennedy final at 86,010; Melbourne partial TPP remains live\n";
        }
    }
}
}

int main(int argc,char const* argv[]) {
    checkFinality();checkRecoverySeparation();checkFixedCountDraws();
    if(argc>1) checkPrivateFeeds(argv[1]);
    std::cout<<"AEC final-result parsing, recovery separation and fixed scenario accounts passed\n";
}
