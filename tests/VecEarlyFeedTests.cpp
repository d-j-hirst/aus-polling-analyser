// Check the information consumed by the Victorian forecast against independent
// candidate totals in a V2 feed. With no paths, use a small fictional count.
// Local operators can also supply candidate XML, location XML and capture XMLs.
#include "../ElectionData.h"
#include "../LiveInputAdapter.h"
#include "../LiveBoothGeographyInput.h"
#include "../LiveFederalDeclarations.h"
#include "../LiveVecCentreGeography.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

using Election = Results2::Election;
using Document = tinyxml2::XMLDocument;
using Node = tinyxml2::XMLElement;
using Type = Results2::VoteType;

void checkCounts(Node const& group, Results2::Seat::VotesByType const& actual)
{
    for (auto c = group.FirstChildElement("Candidate"); c; c = c->NextSiblingElement("Candidate")) {
        int id = c->FirstChildElement("eml:CandidateIdentifier")->IntAttribute("Id");
        auto breakdown = c->FirstChildElement("VotesByType");
        if (!breakdown) {
            assert(!actual.contains(id)); // A district total alone supplies no allocation.
            continue;
        }
        auto const& observed = actual.at(id);
        int total = 0;
        for (auto const& [type, count] : observed) total += count;
        std::map<Type,int> early;
        for (auto v = breakdown->FirstChildElement("Votes"); v; v = v->NextSiblingElement("Votes")) {
            std::string type = v->Attribute("Type");
            if (type == "Early") early[Type::OtherEarly] = v->IntText();
            if (type == "EVCEarly") early[Type::EvcEarly] = v->IntText();
            if (type == "MobileVotingEarly") early[Type::MobileEarly] = v->IntText();
        }
        assert(total == c->FirstChildElement("Votes")->IntText() && !observed.contains(Type::Early));
        for (auto type : {Type::EvcEarly,Type::MobileEarly,Type::OtherEarly}) {
            assert(observed.contains(type) == early.contains(type));
            if (early.contains(type)) assert(observed.at(type) == early.at(type));
        }
    }
}

void checkCapture(Election& election, Document const& capture, std::set<int> const& ordinary)
{
    assert(election.matchesSnapshotElection(capture, Election::Format::VEC));
    election.updateLive(capture, Election::Format::VEC);
    auto contests = capture.RootElement()->FirstChildElement("Election")->FirstChildElement("House")->FirstChildElement("Contests");
    for (auto c = contests->FirstChildElement("Contest"); c; c = c->NextSiblingElement("Contest")) {
        auto const& seat = election.seats.at(c->FirstChildElement("PollingDistrictIdentifier")->IntAttribute("Id"));
        checkCounts(*c->FirstChildElement("FirstPreferences"), seat.fpVotes);
        if (auto tcp = c->FirstChildElement("TwoCandidatePreferred")) checkCounts(*tcp, seat.tcpVotesCandidate);
        for (int id : seat.booths) {
            auto const& booth = election.booths.at(id);
            if (booth.incrementalFp) assert(booth.type == Results2::Booth::Type::Ppvc && booth.parentSeat == seat.id);
            else assert(ordinary.contains(id));
        }
    }
    assert(std::count_if(election.booths.begin(),election.booths.end(),
        [](auto const& row) { return !row.second.incrementalFp; }) == static_cast<int>(ordinary.size()));
    assert(election.invalidLiveCounts.empty());
}

// Exercise every real district's centre allocation with the saved seat FP
// medians as inputs. The sample preference rates check conservation, without
// needing the GUI or pretending these are its election-night preference rates.
void checkCentreMix(std::map<std::string,TurnoutModelIO::EarlyCentre> const& roster,
    nlohmann::json const& bands, size_t mid, int nationals, int greens, int on) {
    namespace Evc = LiveVecCentreGeography;
    Evc::Shares district;
    for (auto const& band : bands) district[band[0].get<int>()]=band[1].at(mid).get<double>()/100;
    district=LiveNewVotes::normalize(std::move(district));
    LivePartialCount::Flows rates;
    for (auto [id,n] : district) rates[id]=id==0 ? 1. : id==1 ? 0. : id==nationals ? .2 : id==greens ? .85 : .5;
    std::vector<Evc::Centre> centres;
    std::set<std::string> labels;
    double volume=0;
    for (auto const& [name,site] : roster) {
        assert(!site.displayName.empty() && labels.insert(site.displayName).second);
        centres.push_back(site); volume+=site.volume;
    }
    std::vector<Evc::Shares> local;
    auto factors = Evc::primaryFactors(centres,district,nationals,greens);
    for (size_t j = 0; j < centres.size(); ++j) {
        local.push_back(Evc::allocate(district,factors[j],nationals,greens,on,centres[j].oneNationContrast));
        if (centres[j].oneNationContrast != 0.)
            assert((local.back().at(on)-district.at(on))*centres[j].oneNationContrast > 0.);
    }
    auto offsets=Evc::preferenceOffsets(centres,local,rates,0.);
    Evc::Shares fp;
    double tpp=0;
    for (size_t i=0; i<centres.size(); ++i) {
        for (auto [id,share] : local[i]) fp[id]+=centres[i].volume/volume*share;
        tpp+=centres[i].volume/volume*LiveNewVotes::firstShare(local[i],rates,offsets[i]);
    }
    for (auto [id,share] : district) assert(std::abs(fp.at(id)-share)<1e-7);
    assert(std::abs(tpp-LiveNewVotes::firstShare(district,rates,0.))<1e-7);
}

// Exercise the same private support material the GUI watcher will load. The
// public test remains runnable with the fictional XML alone.
void checkPrepared(std::filesystem::path const& root, std::filesystem::path const& forecast)
{
    Document candidates, places, zero;
    assert(candidates.LoadFile((root/"downloads/2026vic_candidates.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    assert(places.LoadFile((root/"downloads/2026vic_booths.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    assert(zero.LoadFile((root/"downloads/2026vic_zeros.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    auto load = [](std::filesystem::path const& path) { return nlohmann::json::parse(std::ifstream(path)); };
    auto original = load(root/"analysis/Booth Results/2022vic.json");
    auto previous = load(root/"forecasts/2026vic/live-inputs/historical-baseline.json");
    auto baseline = Election::createVec(previous, candidates, places, "2022vic");
    for (auto const& [id, seat] : baseline.seats) {
        auto const& booths = previous.at(seat.name).at("booths");
        auto const& pooled = original.at(seat.name).at("booths").at("Early Votes");
        assert(!booths.contains("Early Votes"));
        for (auto stage : {"fp","tcp"}) for (auto const& [candidate,votes] : pooled.at(stage).items()) {
            int split = 0;
            for (auto name : {"EVC Early Votes","Mobile Early Votes","Other Early Votes"})
                split += booths.at(name).at(stage).at(candidate).get<int>();
            assert(split == votes.get<int>());
        }
        assert(seat.measuredFpInSyntheticTypes.contains(Type::EvcEarly) == (seat.name != "Narracan"));
        int fp = 0, tcp = 0;
        for (auto const& b : booths) {
            for (auto const& count : b.at("fp")) fp += count.get<int>();
            for (auto const& count : b.at("tcp")) tcp += count.get<int>();
        }
        assert(fp == seat.totalVotesFp() && tcp == seat.totalVotesTcp({}));
    }
    auto current = Election::createVec(candidates, places, "2026vic");
    assert(current.matchesSnapshotElection(zero, Election::Format::VEC));
    current.updateLive(zero, Election::Format::VEC);
    auto artifact = TurnoutModelIO::loadForElection(root, "2026vic");
    auto const& prior = artifact.prior;
    auto snapshot = LiveInputAdapter::identify(current, artifact, "zero-check");
    assert(snapshot.districts.size() == 88);
    assert(snapshot.records.size() == current.booths.size() + 7 * snapshot.districts.size());
    for (auto const& name : prior.seats) {
        double earlyWeight = 0;
        std::set<std::string> earlyTypes;
        for (auto const& u : prior.units) if (prior.seats.at(u.seat) == name && prior.groups.at(u.group) == "early") {
            earlyWeight += u.weight;
            earlyTypes.insert(u.category);
            if (u.name=="EVC early") assert(u.weight==0);
            else if (u.category=="EVC early") assert(u.weight>0 && u.kind=="ppvc" && u.incremental);
        }
        assert((earlyTypes == std::set<std::string>{"EVC early","Mobile early","Other early"}));
        assert(std::abs(earlyWeight - 1) < 1e-12);
    }
    std::cout << "Matched historical candidate counts and " << snapshot.records.size() << " current turnout services.\n";
    if (forecast.empty()) return;

    namespace Geo = LiveBoothGeography;
    auto report = load(forecast).at("report");
    assert(report.at("termCode") == "2026vic");
    auto const& bands = report.at("probabilityBands");
    auto mid = std::find(bands.begin(),bands.end(),50) - bands.begin();
    int on = -1, nationals = -1, greens = -1;
    for (auto const& abbr : report.at("partyAbbr")) {
        if (abbr[1] == "ONP") on = abbr[0];
        if (abbr[1] == "NAT") nationals = abbr[0];
        if (abbr[1] == "GRN") greens = abbr[0];
    }
    assert(on >= 0 && nationals >= 0 && greens >= 0 && mid < int(bands.size()));
    auto roster = Geo::readRoster(load(root/"forecasts/2026vic/live-inputs/one-nation-booths.json"),prior);
    Geo::ModeShifts shifts;
    for (auto [mode,shift] : LiveFederalDeclarations::readPrior(*LiveFederalDeclarations::inputPath(root,"2026vic"),"2026vic"))
        shifts.emplace(LiveData::voteTypeName(mode),shift);
    std::map<std::string,std::string> historicalTypes{{"EVC early","EVC Early Votes"},{"Mobile early","Mobile Early Votes"},
        {"Other early","Other Early Votes"},{"Absent","Absent Votes"},{"Postal","Postal Votes"},
        {"Marked as voted","Marked As Voted Votes"},{"Provisional","Provisional Votes"}};
    int verified = 0, returning = 0;
    for (size_t i = 0; i < report.at("seatNames").size(); ++i) {
        std::string name = report.at("seatNames").at(i);
        checkCentreMix(artifact.earlyCentres.at(name),report.at("seatFpBands").at(i),mid,nationals,greens,on);
        double target = 0;
        for (auto const& row : report.at("seatFpBands").at(i)) if (row[0] == on) target = row[1].at(mid).get<double>()/100;
        assert(target > 0 && target < 1);
        auto const& old = previous.at(name);
        std::string previousCandidate;
        for (auto const& [id,c] : old.at("candidates").items())
            if (c.at("party") == "PAULINE HANSON'S ONE NATION") previousCandidate = id;
        if (!previousCandidate.empty()) ++returning;
        std::vector<Geo::Place> units;
        for (auto const& [key,r] : roster) if (key.first == name) {
            Geo::Place place{r.mode,r.size,r.federal};
            std::string oldName = r.mode == "ordinary" ? key.second : historicalTypes.at(r.mode);
            if (!previousCandidate.empty() && (r.mode!="EVC early" || key.second=="EVC early") && old.at("booths").contains(oldName)) {
                auto const& b = old.at("booths").at(oldName);
                if (!b.value("synthetic",false) || b.value("measured_fp",false)) {
                    place.previousOn = b.at("fp").at(previousCandidate);
                    for (auto const& count : b.at("fp")) place.previousFormal += count.get<int>();
                }
            }
            units.push_back(place);
        }
        auto refs = Geo::baseline(units,target,shifts);
        double votes = 0, formal = 0, homeVotes = 0, homeFormal = 0;
        for (size_t j = 0; j < units.size(); ++j) {
            votes += refs[j].share * units[j].size;
            formal += units[j].size;
            if (units[j].mode == "ordinary" || units[j].mode == "EVC early") {
                homeVotes += refs[j].share * units[j].size;
                homeFormal += units[j].size;
            }
        }
        for (size_t j = 0; j < units.size(); ++j) if (shifts.contains(units[j].mode)) {
            double difference = LivePartialCount::logOdds(refs[j].share) - LivePartialCount::logOdds(homeVotes/homeFormal);
            assert(std::abs(difference-shifts.at(units[j].mode)) < 1e-11);
        }
        assert(std::abs(votes/formal - target) < 1e-12);
        ++verified;
    }
    assert(verified == 88 && returning == 5);
    std::cout << "Reproduced all " << verified << " published district medians with the prepared turnout and geography.\n";
}

// Keep both representations of the same batch in the source. Check the
// effective records, including a later district-only delivery where ordinary
// recovery of the older venue would otherwise double count those ballots.
void checkVenuePartition(Election election) {
    using namespace LiveInputRecovery;
    TurnoutModelIO::Artifact input;
    auto& p=input.prior;
    p.election="2026vic"; p.seats={"Example"}; p.groups={"early"}; p.enrolment={1000}; p.counts={{500}};
    for (auto const& [name,role] : std::vector<std::pair<std::string,std::string>>{
            {"EVC early","declaration"},{"Early venue","ppvc"},{"School","ordinary"}}) {
        TurnoutModel::Unit u;
        u.name=name; u.kind=role; u.category=role=="ordinary" ? role : "EVC early";
        u.incremental=role=="ppvc"; u.weight=role=="ppvc" ? 1 : 0;
        p.units.push_back(u);
    }
    input.earlyCentres["Example"]["Early venue"]={4,100,{},{}};
    auto& seat=election.seats.at(2);
    auto& venue=election.booths.at(seat.booths.back());
    assert(venue.incrementalFp && venue.name=="Early venue");
    seat.fpVotes.clear(); seat.tcpVotesCandidate.clear();
    election.liveCountsRead=false;
    election.invalidLiveCounts.clear();
    election.sourceTime="2026-11-25T18:00:00";
    seat.fpVotes[11][Type::EvcEarly]=50; seat.fpVotes[12][Type::EvcEarly]=50;
    seat.tcpVotesCandidate=seat.fpVotes;
    venue.fpVotes={{11,20},{12,30}}; venue.tcpVotesCandidate=venue.fpVotes;
    auto read=[&]() { return LiveInputAdapter::identify(election,input,"test"); };
    auto identified=read();
    assert((identified.records[0].fp.votes==Counts{{11,30},{12,20}}));
    assert((identified.records[1].tcp.votes==Counts{{11,20},{12,30}}));
    auto first=recover(std::move(identified),{}).snapshot;
    election.sourceTime="2026-11-25T18:05:00";
    venue.fpVotes.clear(); venue.tcpVotesCandidate.clear();
    auto districtOnly=recover(read(),first).snapshot;
    assert(!districtOnly.records[1].fp.votes && !districtOnly.records[1].tcp.votes);
    assert(total(districtOnly.records[0].fp.votes)==100 && total(districtOnly.records[0].tcp.votes)==100);
    venue.fpVotes={{11,20},{12,30}};
    // The FP location is known but the whole TCP is unlocated. A split would
    // leave 100 TCP against 50 residual FP. Use the coherent district account.
    auto mixed=read();
    assert(!mixed.records[1].fp.votes && total(mixed.records[0].fp.votes)==100);
    seat.tcpVotesCandidate[11][Type::EvcEarly]=45;
    seat.tcpVotesCandidate[12][Type::EvcEarly]=35;
    venue.tcpVotesCandidate={{11,15},{12,15}};
    auto partial=recover(read(),first).snapshot;
    assert(total(partial.records[1].tcp.votes)==30 && total(partial.records[1].fp.votes)==50);
    venue.fpVotes={{11,75},{12,0}};
    auto contradictory=recover(read(),first).snapshot;
    assert(!contradictory.records[1].fp.votes && total(contradictory.records[0].fp.votes)==100);
    seat.fpVotes.clear(); seat.tcpVotesCandidate.clear();
    venue.fpVotes={{11,20},{12,30}};
    auto detailOnly=recover(read(),first).snapshot;
    assert(!detailOnly.records[0].fp.votes && total(detailOnly.records[1].fp.votes)==50);
    venue.fpVotes.clear(); venue.tcpVotesCandidate.clear();
    auto missing=recover(read(),first).snapshot;
    assert(missing.records[0].identity==first.records[0].identity);
    assert(total(missing.records[0].fp.votes)==50 && total(missing.records[1].fp.votes)==50);
    assert(missing.records[1].fp.status==Status::Restored && missing.records[1].fp.observedAt==first.sourceTime);
    assert(!sameRecordIdentity(first.records[0],districtOnly.records[0]));
    // An unusable inclusive FP total cannot suppress a valid, located
    // count, even when the district TCP is positive.
    seat.fpVotes[11][Type::EvcEarly]=1200; seat.fpVotes[12][Type::EvcEarly]=0;
    seat.tcpVotesCandidate[11][Type::EvcEarly]=300; seat.tcpVotesCandidate[12][Type::EvcEarly]=20;
    venue.fpVotes={{11,20},{12,30}}; venue.tcpVotesCandidate=venue.fpVotes;
    auto invalidTotal=recover(read(),first).snapshot;
    assert(total(invalidTotal.records[1].fp.votes)==50 && total(invalidTotal.records[1].tcp.votes)==50);
}

void checkCentreTurnout() {
    // A district-only delivery and a fully located delivery of the same
    // ballots imply the same total home, mobile and other early turnout.
    TurnoutModel::Prior prior;
    prior.election="2026vic"; prior.seats={"Example"}; prior.subdivisions={"Vic"};
    prior.groups={"early","ordinary"}; prior.enrolment={2000};
    prior.totals={{1000},{1100},{1200}}; prior.counts={{500,500},{600,500},{700,500}};
    auto unit=[](std::string name, std::string kind, std::string category, double weight) {
        TurnoutModel::Unit u; u.name=name; u.kind=kind; u.category=category; u.weight=weight;
        u.group=kind=="ordinary" ? 1 : 0; u.incremental=kind=="ppvc"; return u;
    };
    prior.units={unit("EVC early","declaration","EVC early",0),
        unit("Town","ppvc","EVC early",.30),unit("City","ppvc","EVC early",.45),
        unit("Mobile early","declaration","Mobile early",.05),unit("Other early","declaration","Other early",.20),
        unit("School","ordinary","Ordinary",1.)};
    auto pooled=prior.units;
    pooled.erase(pooled.begin()+1,pooled.begin()+3);
    pooled[0].weight=.75; pooled[0].counted=260;
    auto reference=TurnoutModel::update(prior,pooled,{false});
    for (double located : {0.,130.,260.}) {
        auto split=prior.units;
        split[0].counted=260-located; split[1].counted=located;
        auto result=TurnoutModel::update(prior,split,{false});
        for (size_t r=0; r<prior.totals.size(); ++r) {
            assert(result.remaining[r][0]==0 && result.unitCounts[r][0]==260-located);
            assert(std::abs(result.unitCounts[r][1]+result.unitCounts[r][2]+260-located-reference.unitCounts[r][0])<1e-7);
            assert(std::abs(result.unitCounts[r][3]-reference.unitCounts[r][1])<1e-7);
            assert(std::abs(result.unitCounts[r][4]-reference.unitCounts[r][2])<1e-7);
        }
    }
    auto units=prior.units;
    units[0].counted=120;
    auto time="2026-11-25T19:00:00";
    LiveInputRecovery::RecordStatuses states;
    for (auto const& u : units) if (u.kind!="ppvc") {
        auto& fp=states["Example/"+u.kind+"/"+u.name].first;
        fp.status=LiveInputRecovery::Status::Current; fp.available=true; fp.observedAt=time;
    }
    auto activity=LiveInputRecovery::freshObservation(prior,units,states,time);
    assert(activity.counts.at({"Example","EVC early"})==120);
    assert(activity.counts.at({"Example","allocation:early"})==120);
}

// Operator-only capture check. Compare the candidate sums after normal
// recovery with the XML district total, and verify a repeated source updates
// the same centre roster. This runs no forecast and writes no count history.
void checkPreparedCaptures(std::filesystem::path const& root, int n, char** files) {
    Document candidates,places;
    assert(candidates.LoadFile((root/"downloads/2026vic_candidates.xml").string().c_str())==tinyxml2::XML_SUCCESS);
    assert(places.LoadFile((root/"downloads/2026vic_booths.xml").string().c_str())==tinyxml2::XML_SUCCESS);
    auto election=Election::createVec(candidates,places,"2026vic");
    auto input=TurnoutModelIO::loadForElection(root,"2026vic");
    std::optional<LiveInputRecovery::Snapshot> previous;
    size_t positive=0, disclosed=0;
    for (int i=0; i<n; ++i) {
        Document capture;
        assert(capture.LoadFile(files[i])==tinyxml2::XML_SUCCESS);
        election.updateLive(capture,Election::Format::VEC);
        auto identified=LiveInputAdapter::identify(election,input,"capture-check");
        auto effective=LiveInputRecovery::recover(std::move(identified),previous).snapshot;
        for (auto const& [id,seat] : election.seats) for (bool tcp : {false,true}) {
            LiveInputRecovery::Counts combined;
            for (auto const& r : effective.records) if (r.district==seat.name &&
                (r.name=="EVC early" || input.earlyCentres.at(seat.name).contains(r.name))) {
                auto const& count=tcp ? r.tcp : r.fp;
                if (r.role=="ppvc") {
                    auto found=std::find_if(seat.booths.begin(),seat.booths.end(),[&](int key){ return election.booths.at(key).name==r.name; });
                    assert(found!=seat.booths.end());
                    auto const& raw=election.booths.at(*found);
                    auto const& votes=tcp ? raw.tcpVotesCandidate : raw.fpVotes;
                    if (std::any_of(votes.begin(),votes.end(),[](auto row){ return row.second>0; })) ++disclosed;
                    if (LiveInputRecovery::total(count.votes)>0) {
                        if (count.fresh()) assert((LiveInputRecovery::Counts(votes.begin(),votes.end())==*count.votes));
                        ++positive;
                    }
                }
                if (count.votes) for (auto [candidate,v] : *count.votes) combined[candidate]+=v;
            }
            auto const& district=tcp ? seat.tcpVotesCandidate : seat.fpVotes;
            for (auto const& [candidate,categories] : district) if (categories.contains(Type::EvcEarly))
                assert(combined[candidate]==categories.at(Type::EvcEarly));
        }
        LiveInputAdapter::apply(election,effective,{});
        previous=std::move(effective);
    }
    std::cout << "Reconciled FP and TCP in " << n << " captures. Positive venue records: " << disclosed
        << " in the feed, " << positive << " after recovery.\n";
}

int main(int argc, char** argv) try
{
    Document candidates, places;
    if (argc>=4 && std::string(argv[1])=="--captures") {
        checkPreparedCaptures(argv[2],argc-3,argv+3);
        return 0;
    }
    if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--prepared") {
        checkPrepared(argv[2],argc == 4 ? argv[3] : "");
        return 0;
    }
    if (argc == 1) {
        candidates.Parse(R"(<EML><CandidateList><EventIdentifier Id="1"/><Election><Contest>
            <PollingDistrictIdentifier Id="2"/><ContestIdentifier><ContestName>Example District</ContestName></ContestIdentifier>
            <Enrolment>1000</Enrolment><Candidate><CandidateIdentifier Id="11"><CandidateName>Example</CandidateName>
            </CandidateIdentifier></Candidate><Candidate><CandidateIdentifier Id="12"><CandidateName>Other</CandidateName>
            </CandidateIdentifier><Affiliation><AffiliationIdentifier Id="99">
              <RegisteredName>Pauline Hanson's One Nation</RegisteredName>
            </AffiliationIdentifier></Affiliation></Candidate></Contest></Election></CandidateList></EML>)");
        places.Parse(R"(<PollingDistrictList><PollingDistrict><PollingDistrictIdentifier Id="2"/><PollingPlaces>
            <PollingPlace><PollingPlaceIdentifier Id="3" Name="School" Classification="VotingCentre"/></PollingPlace>
            <PollingPlace><PollingPlaceIdentifier Id="4" Name="Early venue" Classification="EarlyVotingCentre"/></PollingPlace>
            <PollingPlace><PollingPlaceIdentifier Id="5" Name="Mobile" Classification="MobileVoting"/></PollingPlace>
            </PollingPlaces></PollingDistrict></PollingDistrictList>)");
    } else {
        assert(argc >= 4);
        assert(candidates.LoadFile(argv[1]) == tinyxml2::XML_SUCCESS && places.LoadFile(argv[2]) == tinyxml2::XML_SUCCESS);
    }
    auto election = Election::createVec(candidates, places, "2026vic");
    if (argc==1) { checkVenuePartition(election); checkCentreTurnout(); }
    if (argc == 1) assert(election.parties.at(99).shortCode == "ONP");
    std::set<int> ordinary;
    for (auto d = places.RootElement()->FirstChildElement("PollingDistrict"); d; d = d->NextSiblingElement("PollingDistrict"))
        for (auto p = d->FirstChildElement("PollingPlaces")->FirstChildElement("PollingPlace"); p; p = p->NextSiblingElement("PollingPlace")) {
            auto id = p->FirstChildElement("PollingPlaceIdentifier");
            if (id->Attribute("Classification","VotingCentre")) ordinary.insert(id->IntAttribute("Id"));
        }
    if (argc == 1) {
        std::string candidate = R"(<Candidate><eml:CandidateIdentifier Id="11"/><Votes>20</Votes><VotesByType>
            <Votes Type="EVCEarly">7</Votes><Votes Type="MobileVotingEarly">3</Votes><Votes Type="Ordinary">4</Votes>
            <Votes Type="Early">6</Votes></VotesByType></Candidate>)";
        std::string xml = R"(<MediaFeed><eml:EventIdentifier Id="1"><eml:EventName>Example</eml:EventName></eml:EventIdentifier>
            <Election><House><Contests><Contest><PollingDistrictIdentifier Id="2"><Name>Example District</Name></PollingDistrictIdentifier>
            <Enrolment>1000</Enrolment><FirstPreferences>)" + candidate + "</FirstPreferences><TwoCandidatePreferred>" + candidate +
            R"(</TwoCandidatePreferred><PollingPlaces><PollingPlace><PollingPlaceIdentifier Id="4"/></PollingPlace>
            <PollingPlace><PollingPlaceIdentifier Id="5"/></PollingPlace></PollingPlaces></Contest></Contests></House></Election></MediaFeed>)";
        Document capture;
        capture.Parse(xml.c_str());
        checkCapture(election, capture, ordinary);
        checkCapture(election, capture, ordinary);
        auto const& tcp = election.seats.at(2).tcpVotes.at(-1);
        assert(tcp.at(Type::EvcEarly) == 7 && tcp.at(Type::MobileEarly) == 3 && tcp.at(Type::OtherEarly) == 6);
        // A lost or malformed mobile row must not erase the measured home or residual rows.
        auto damaged = xml;
        damaged[damaged.find("Early\">3")+7] = 'x';
        capture.Parse(damaged.c_str());
        election.updateLive(capture, Election::Format::VEC);
        assert(election.invalidLiveCounts.contains({Election::countKey(2,0,Type::MobileEarly),false}));
        assert(!election.invalidLiveCounts.contains({Election::countKey(2,0,Type::EvcEarly),false}));
        assert(election.seats.at(2).fpVotes.at(11).at(Type::OtherEarly) == 6);
        // The old V1 feed used Early for the whole pool. A pre-V2 replay
        // using an ordinary-only preload must retain its original category ID.
        auto oldPlaces = places.RootElement()->FirstChildElement("PollingDistrict")->FirstChildElement("PollingPlaces");
        while (oldPlaces->FirstChildElement() != oldPlaces->LastChildElement())
            oldPlaces->DeleteChild(oldPlaces->LastChildElement());
        auto legacy = Election::createVec(candidates,places,"2022vic");
        capture.Parse(xml.c_str());
        auto contest = capture.RootElement()->FirstChildElement("Election")->FirstChildElement("House")
            ->FirstChildElement("Contests")->FirstChildElement("Contest");
        contest->FirstChildElement("PollingPlaces")->DeleteChildren();
        for (auto stage : {"FirstPreferences","TwoCandidatePreferred"}) {
            auto c = contest->FirstChildElement(stage)->FirstChildElement("Candidate");
            auto votes = c->FirstChildElement("VotesByType");
            votes->DeleteChild(votes->FirstChildElement());
            votes->DeleteChild(votes->FirstChildElement());
            c->FirstChildElement("Votes")->SetText(10);
        }
        legacy.updateLive(capture, Election::Format::VEC);
        assert(legacy.seats.at(2).fpVotes.at(11).at(Type::Early) == 6);
        assert(!legacy.seats.at(2).fpVotes.at(11).contains(Type::OtherEarly));
    } else {
        for (int i = 3; i < argc; ++i) {
            Document capture;
            assert(capture.LoadFile(argv[i]) == tinyxml2::XML_SUCCESS);
            checkCapture(election, capture, ordinary);
        }
    }
    std::cout << "VEC district early counts and ordinary-location roster verified.\n";
    return 0;
} catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
