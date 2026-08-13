#include "ElectionData.h"

#include "General.h"
#include "Log.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>

std::map<std::string, Results2::VoteType> typeNameToVoteType = {
  {"Ordinary", Results2::VoteType::Ordinary},
  {"PP", Results2::VoteType::Ordinary},
  {"PR", Results2::VoteType::Ordinary},
  {"Absent", Results2::VoteType::Absent},
  {"Provisional", Results2::VoteType::Provisional},
  {"Enrolment/Provisional", Results2::VoteType::Provisional},
  {"PrePoll", Results2::VoteType::PrePoll},
  {"Postal", Results2::VoteType::Postal},
  {"Early", Results2::VoteType::Early},
  {"iVote", Results2::VoteType::IVote}
};

// WAEC doesn't give us any numerical IDs for the booths/seats, so we need to generate our own
// Create a simple hash function as a named lambda
int hashName(const std::string& name) {
  std::size_t hash = 0;
  for (char c : name) {
    hash = hash * 31 + c; // Multiply by prime number and add character value
  }
  return static_cast<int>(hash & 0x7FFFFFFF); // Ensure positive value by masking
};

namespace {

using CandidateMatchKey = std::pair<std::string, std::string>;

[[noreturn]] void throwInvalidXml(
  std::string const& context,
  std::string const& problem)
{
  throw std::runtime_error("Invalid election XML at " + context + ": " + problem);
}

tinyxml2::XMLElement const& requiredChild(
  tinyxml2::XMLNode const& parent,
  char const* childName,
  std::string const& context)
{
  auto const* child = parent.FirstChildElement(childName);
  if (!child) throwInvalidXml(context, "missing <" + std::string(childName) + ">");
  return *child;
}

std::string requiredText(
  tinyxml2::XMLElement const& element,
  std::string const& context)
{
  auto const* text = element.GetText();
  if (!text || !*text) throwInvalidXml(context, "missing text");
  return text;
}

std::string requiredAttribute(
  tinyxml2::XMLElement const& element,
  char const* attributeName,
  std::string const& context)
{
  auto const* value = element.Attribute(attributeName);
  if (!value || !*value) {
    throwInvalidXml(context, "missing attribute '" + std::string(attributeName) + "'");
  }
  return value;
}

int checkedXmlInteger(
  tinyxml2::XMLError result,
  int value,
  std::string const& context,
  int minimum = std::numeric_limits<int>::min())
{
  if (result != tinyxml2::XML_SUCCESS) throwInvalidXml(context, "expected an integer");
  if (value < minimum) {
    throwInvalidXml(context, "integer " + std::to_string(value) +
      " is below the minimum " + std::to_string(minimum));
  }
  return value;
}

int requiredIntText(
  tinyxml2::XMLElement const& element,
  std::string const& context,
  int minimum = std::numeric_limits<int>::min())
{
  int value = 0;
  auto const result = element.QueryIntText(&value);
  return checkedXmlInteger(result, value, context, minimum);
}

int integerTextOrZero(
  tinyxml2::XMLElement const& element,
  std::string const& context)
{
  if (!element.GetText()) return 0;
  return requiredIntText(element, context, 0);
}

int requiredIntAttribute(
  tinyxml2::XMLElement const& element,
  char const* attributeName,
  std::string const& context,
  int minimum = std::numeric_limits<int>::min())
{
  int value = 0;
  auto const result = element.QueryIntAttribute(attributeName, &value);
  return checkedXmlInteger(
    result, value, context + "/@" + attributeName, minimum);
}

float requiredFloatText(
  tinyxml2::XMLElement const& element,
  std::string const& context,
  float minimum,
  float maximum)
{
  float value = 0.0f;
  if (element.QueryFloatText(&value) != tinyxml2::XML_SUCCESS ||
    !std::isfinite(value)) {
    throwInvalidXml(context, "expected a finite number");
  }
  if (value < minimum || value > maximum) {
    throwInvalidXml(context, "number " + std::to_string(value) +
      " is outside [" + std::to_string(minimum) + ", " +
      std::to_string(maximum) + "]");
  }
  return value;
}

int requiredIntegerString(
  std::string const& text,
  std::string const& context,
  size_t offset = 0,
  int minimum = std::numeric_limits<int>::min())
{
  if (offset >= text.size()) throwInvalidXml(context, "expected an integer");
  try {
    size_t consumed = 0;
    int const value = std::stoi(text.substr(offset), &consumed);
    if (consumed != text.size() - offset) throwInvalidXml(context, "expected an integer");
    if (value < minimum) {
      throwInvalidXml(context, "integer " + std::to_string(value) +
        " is below the minimum " + std::to_string(minimum));
    }
    return value;
  }
  catch (std::invalid_argument const&) {
    throwInvalidXml(context, "expected an integer");
  }
  catch (std::out_of_range const&) {
    throwInvalidXml(context, "integer is outside the supported range");
  }
}

struct SaDeclarationCategory {
  std::string boothName;
  Results2::VoteType voteType;
  double share;
  double laborTcpOffset;
  double laborFpOffset;
  double liberalFpOffset;
  double greensFpOffset;
  bool distributeAcrossPpvc = false;
};

const std::array<SaDeclarationCategory, 6> SaDeclarationBaseCategories = { {
  { "PPVCs", Results2::VoteType::Ordinary, 244615.0 / 467973.0, -0.001588, 0.003526, 0.003135, -0.006121, true },
  { "Polling Day Absent Ordinary Votes", Results2::VoteType::Absent, 36139.0 / 467973.0, 0.066084, -0.034877, -0.102218, 0.092173, false },
  { "Early Voting Absent Ordinary Votes", Results2::VoteType::PrePoll, 25988.0 / 467973.0, 0.019996, -0.025292, -0.039956, 0.039403, false },
  { "Postal Votes", Results2::VoteType::Postal, 157070.0 / 467973.0, -0.018724, 0.005745, 0.028383, -0.019439, false },
  { "Polling Day Declaration Votes", Results2::VoteType::Provisional, 0.5 * (4161.0 / 467973.0), 0.101303, 0.036724, -0.118374, 0.046955, false },
  { "Early Voting Declaration Votes", Results2::VoteType::EarlyProvisional, 0.5 * (4161.0 / 467973.0), 0.101303, 0.036724, -0.118374, 0.046955, false },
} };

const std::array<SaDeclarationCategory, 2> SaDeclarationExtraCategories = { {
  { "Electoral Visitor/Mobile Declaration Votes", Results2::VoteType::EVM, 0.005, 0.0, 0.0, 0.0, 0.0, false },
  { "Telephone/Interstate/Overseas Declaration Votes", Results2::VoteType::TIO, 0.005, 0.0, 0.0, 0.0, 0.0, false },
} };

double clampShare(double value)
{
  return std::clamp(value, 0.0, 1.0);
}

std::vector<int> allocateVotes(int totalVotes, std::vector<double> weights, int minimumEach = 0)
{
  if (weights.empty()) return {};
  int minimumTotal = int(weights.size()) * minimumEach;
  if (totalVotes < minimumTotal) totalVotes = minimumTotal;
  std::vector<int> votes(weights.size(), minimumEach);
  int remainingVotes = totalVotes - minimumTotal;
  if (remainingVotes <= 0) return votes;

  double weightSum = std::accumulate(weights.begin(), weights.end(), 0.0);
  if (weightSum <= 0.0) {
    std::fill(weights.begin(), weights.end(), 1.0);
    weightSum = double(weights.size());
  }

  std::vector<double> remainders(weights.size(), 0.0);
  int allocatedVotes = 0;
  for (size_t index = 0; index < weights.size(); ++index) {
    double exactVotes = remainingVotes * weights[index] / weightSum;
    int extraVotes = int(std::floor(exactVotes));
    votes[index] += extraVotes;
    allocatedVotes += extraVotes;
    remainders[index] = exactVotes - extraVotes;
  }

  int leftoverVotes = remainingVotes - allocatedVotes;
  std::vector<size_t> order(weights.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
    [&](size_t lhs, size_t rhs) { return remainders[lhs] > remainders[rhs]; });
  for (int vote = 0; vote < leftoverVotes; ++vote) {
    votes[order[vote % order.size()]] += 1;
  }

  return votes;
}

std::string ecsaCandidatePartyBucket(std::string const& partyCode)
{
  static std::set<std::string> const majorPartyCodes = { "ALP", "LIB", "GRN", "ONP" };
  return majorPartyCodes.contains(partyCode) ? partyCode : "OTH";
}

CandidateMatchKey ecsaCandidateMatchKey(std::string const& candidateName, std::string const& partyCode)
{
  return { candidateName, ecsaCandidatePartyBucket(partyCode) };
}

std::string qecCandidateKey(int seatId, std::string_view candidateName)
{
  return std::to_string(seatId) + "\x1f" + std::string(candidateName);
}

std::string nswecCandidateKey(int seatId, std::string_view candidateName)
{
  return std::to_string(seatId) + "\x1e" + std::string(candidateName);
}

void registerEcsaCandidateMatch(std::map<CandidateMatchKey, int>& candidateMatchToId,
  Results2::Candidate const& candidate, Results2::Party const& party)
{
  candidateMatchToId[ecsaCandidateMatchKey(candidate.name, party.shortCode)] = candidate.id;
}

std::optional<int> findEcsaCandidateMatch(std::map<CandidateMatchKey, int> const& candidateMatchToId,
  std::string const& candidateName, std::string const& partyCode)
{
  auto found = candidateMatchToId.find(ecsaCandidateMatchKey(candidateName, partyCode));
  if (found == candidateMatchToId.end()) return std::nullopt;
  return found->second;
}

}

Results2::Election Results2::Election::createAec(tinyxml2::XMLDocument const& xml, std::string const& termCode)
{
  Election election(termCode);
  election.update(xml);
  return election;
}

Results2::Election Results2::Election::createVec(nlohmann::json const& results, tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths, std::string const& termCode)
{
  Election election(termCode);
  election.update2022VicPrev(results, input_candidates, input_booths);
  return election;
}

Results2::Election Results2::Election::createVec(tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths, std::string const& termCode)
{
  Election election(termCode);	
  election.preload2022Vic(input_candidates, input_booths, true);
  return election;
}

Results2::Election Results2::Election::createNswec(nlohmann::json const& results, tinyxml2::XMLDocument const& zeros, std::string const& termCode)
{
  Election election(termCode);
  election.preloadNswec(results, zeros, true);
  return election;
}

Results2::Election Results2::Election::createQec(nlohmann::json const& results, tinyxml2::XMLDocument const& zeros, std::string const& termCode)
{
  Election election(termCode);
  election.preloadQec(results, zeros);
  return election;
}

Results2::Election Results2::Election::createWaec(tinyxml2::XMLDocument const& candidatesXml, tinyxml2::XMLDocument const& resultsXml, std::string const& termCode)
{
  Election election(termCode);
  election.preloadWaec(candidatesXml, resultsXml);
  return election;
}

Results2::Election Results2::Election::createEcsa(nlohmann::json const& results, tinyxml2::XMLDocument const& zeros, std::string const& termCode)
{
  Election election(termCode);
  election.preloadEcsa(results, zeros);
  return election;
}

void Results2::Election::preload2022Vic(tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths, bool includeSeatBooths)
{
  const std::map<std::string, std::string> shortCodes = {
    {"The Australian Greens - Victoria", "GRN"},
    {"Australian Labor Party - Victorian Branch", "ALP"},
    {"Liberal Party of Australia - Victorian Division", "LNP"},
    {"National Party of Australia - Victoria", "LNP"}
  };
  auto const& eml = requiredChild(input_candidates, "EML", "VEC candidate preload");
  auto const& candidateList = requiredChild(eml, "CandidateList", "VEC candidate preload/EML");
  auto const& election = requiredChild(candidateList, "Election", "VEC candidate preload/EML/CandidateList");
  auto currentContest = election.FirstChildElement("Contest");
  if (!currentContest) throwInvalidXml("VEC candidate preload/EML/CandidateList/Election", "missing <Contest>");
  while (currentContest) {
    Seat seat;
    auto const& contestIdentifier = requiredChild(*currentContest,
      "PollingDistrictIdentifier", "VEC candidate preload/Contest");
    seat.id = requiredIntAttribute(contestIdentifier, "Id",
      "VEC candidate preload/Contest/PollingDistrictIdentifier", 0);
    if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
    auto const& generalIdentifier = requiredChild(*currentContest,
      "ContestIdentifier", "VEC candidate preload/Contest");
    std::string contestName = requiredText(
      requiredChild(generalIdentifier, "ContestName",
        "VEC candidate preload/Contest/ContestIdentifier"),
      "VEC candidate preload/Contest/ContestIdentifier/ContestName");
    if (contestName.length() < 9) {
      throwInvalidXml("VEC candidate preload/Contest/ContestIdentifier/ContestName",
        "name is too short to contain the expected suffix");
    }
    seat.name = contestName.substr(0, contestName.length() - 9);
    auto const seatContext = "VEC candidate preload/Contest[" + seat.name + "]";
    seat.enrolment = requiredIntText(
      requiredChild(*currentContest, "Enrolment", seatContext),
      seatContext + "/Enrolment", 0);
    auto currentCandidate = currentContest->FirstChildElement("Candidate");
    while (currentCandidate) {
      Candidate candidate;
      auto const& candidateIdentifier = requiredChild(*currentCandidate,
        "CandidateIdentifier", seatContext + "/Candidate");
      candidate.id = requiredIntAttribute(candidateIdentifier, "Id",
        seatContext + "/Candidate/CandidateIdentifier", 0);
      candidate.name = requiredText(
        requiredChild(candidateIdentifier, "CandidateName",
          seatContext + "/Candidate/CandidateIdentifier"),
        seatContext + "/Candidate/CandidateIdentifier/CandidateName");
      auto affiliationEl = currentCandidate->FirstChildElement("Affiliation");
      if (affiliationEl) {
        auto const& affiliationIdentifier = requiredChild(*affiliationEl,
          "AffiliationIdentifier", seatContext + "/Candidate/Affiliation");
        candidate.party = requiredIntAttribute(affiliationIdentifier, "Id",
          seatContext + "/Candidate/Affiliation/AffiliationIdentifier", 0);
        if (!parties.contains(candidate.party)) {
          parties[candidate.party] = Party();
          parties[candidate.party].id = candidate.party;
          parties[candidate.party].name =
            requiredText(
              requiredChild(affiliationIdentifier, "RegisteredName",
                seatContext + "/Candidate/Affiliation/AffiliationIdentifier"),
              seatContext + "/Candidate/Affiliation/AffiliationIdentifier/RegisteredName");
          if (shortCodes.contains(parties[candidate.party].name)) {
            parties[candidate.party].shortCode = shortCodes.at(parties[candidate.party].name);
          }
        }
      }
      else {
        candidate.party = Candidate::Independent;
        parties[Candidate::Independent].id = Candidate::Independent;
        parties[Candidate::Independent].shortCode = "IND";
        parties[Candidate::Independent].name = "Independent";
      }
      candidates[candidate.id] = candidate;

      currentCandidate = currentCandidate->NextSiblingElement("Candidate");
    }

    seats[seat.id] = seat;
    currentContest = currentContest->NextSiblingElement("Contest");
  }
  auto const& pollingDistrictList = requiredChild(input_booths,
    "PollingDistrictList", "VEC booth preload");
  auto currentPollingDistrict = pollingDistrictList.FirstChildElement("PollingDistrict");
  if (!currentPollingDistrict) throwInvalidXml("VEC booth preload/PollingDistrictList", "missing <PollingDistrict>");
  while (currentPollingDistrict) {
    auto const& districtIdentifier = requiredChild(*currentPollingDistrict,
      "PollingDistrictIdentifier", "VEC booth preload/PollingDistrict");
    int seatId = requiredIntAttribute(districtIdentifier, "Id",
      "VEC booth preload/PollingDistrict/PollingDistrictIdentifier", 0);
    auto const districtContext = "VEC booth preload/PollingDistrict[" + std::to_string(seatId) + "]";
    auto const& pollingPlaces = requiredChild(*currentPollingDistrict, "PollingPlaces", districtContext);
    auto currentBooth = pollingPlaces.FirstChildElement("PollingPlace");
    while (currentBooth) {
      Booth booth;
      auto const& boothIdentifier = requiredChild(*currentBooth,
        "PollingPlaceIdentifier", districtContext + "/PollingPlaces/PollingPlace");
      booth.id = requiredIntAttribute(boothIdentifier, "Id",
        districtContext + "/PollingPlaces/PollingPlace/PollingPlaceIdentifier", 0);
      booth.name = requiredAttribute(boothIdentifier, "Name",
        districtContext + "/PollingPlaces/PollingPlace/PollingPlaceIdentifier");
      booth.parentSeat = seatId;
      booths[booth.id] = booth;
      currentBooth = currentBooth->NextSiblingElement("PollingPlace");
      if (includeSeatBooths) {
        seats[seatId].booths.push_back(booth.id);
      }
    }
    currentPollingDistrict = currentPollingDistrict->NextSiblingElement("PollingDistrict");
  }
}

void Results2::Election::update2022VicPrev(nlohmann::json const& results, tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths)
{
  preload2022Vic(input_candidates, input_booths);
  std::map<std::string, int> seatNameToId;
  for (auto [seatId, seat] : seats) {
    if (seatNameToId.contains(seat.name)) {
      logger << "Warning: duplicate name for " << seat.name << "!\n";
    }
    else {
      seatNameToId[seat.name] = seatId;
    }
  }
  for (auto [candidateId, candidate] : candidates) {
    if (candidateNameToId.contains(candidate.name)) {
      logger << "Warning: duplicate name for " << candidate.name << "!\n";
    }
    else {
      candidateNameToId[candidate.name] = candidateId;
    }
  }
  std::set<std::string> seenBooths;
  std::set<std::string> ambiguousBooths;
  for (auto [boothId, booth] : booths) {
    if (seenBooths.contains(booth.name)) {
      ambiguousBooths.emplace(booth.name);
    }
    else {
      seenBooths.emplace(booth.name);
    }
  }
  std::map<std::string, int> boothNameToId;
  std::map<std::pair<std::string, std::string>, int> ambiguousBoothNameToId;
  for (auto [boothId, booth] : booths) {
    if (ambiguousBooths.contains(booth.name)) {
      ambiguousBoothNameToId[{booth.name, seats[booth.parentSeat].name}] = boothId;
    }
    else {
      boothNameToId[booth.name] = boothId;
    }
  }
  //PA_LOG_VAR(boothNameToId);
  //PA_LOG_VAR(ambiguousBoothNameToId);
  int dummyCandidateId = -100000; // Low numbers that will never be mistaken for an official id
  int dummyBoothId = -100000; // Low numbers that will never be mistaken for an official id
  // Not worth soft-coding this
  const std::map<std::string, int> partyIds = {
    {"ANIMAL JUSTICE PARTY",  25},
    {"LIBERAL", 8},
    {"FIONA PATTEN'S REASON PARTY", 20},
    {"SUSTAINABLE AUSTRALIA", 91},
    {"AUSTRALIAN GREENS", 4},
    {"AUSTRALIAN LABOR PARTY", 5},
    {"LABOUR DLP", 7},
    {"SHOOTERS, FISHERS & FARMERS VIC", 28},
    {"VICTORIAN SOCIALISTS", 90},
    {"THE NATIONALS", 9},
    {"DERRYN HINCH'S JUSTICE PARTY", 89},
    {"LIBERAL DEMOCRATS", 30},
    {"TRANSPORT MATTERS", 87},
    {"AUSSIE BATTLER PARTY", -1},
    {"AUSTRALIAN COUNTRY PARTY", -1},
    {"AUSTRALIAN LIBERTY ALLIANCE", -1},
    {"INDEPENDENT", -1}
  };
  std::set<int> matchedIds;
  for (auto const& [seatName, seatValue] : results.items()) {
    int seatId = -1;
    if (seatNameToId.contains(seatName)) {
      seatId = seatNameToId[seatName];
    }
    std::map<int, int> indexToId;
    for (auto const& [candIndex, candValue] : seatValue["candidates"].items()) {
      int candIndexI = std::stoi(candIndex);
      auto candidateName = candValue["name"];
      auto party = candValue["party"];
      if (candidateNameToId.contains(candidateName)) {
        indexToId[candIndexI] = candidateNameToId[candidateName];
      }
      else {
        indexToId[candIndexI] = dummyCandidateId;
        Candidate candidate;
        candidate.id = dummyCandidateId;
        candidate.name = candidateName;
        candidate.party = partyIds.at(party);
        candidates[candidate.id] = candidate;
        --dummyCandidateId;
      }
    }
    for (auto const& [boothName, boothValue] : seatValue["booths"].items()) {
      if (boothName.find("Votes") != std::string::npos) {
        VoteType voteType = VoteType::Invalid;
        if (boothName == "Postal Votes") voteType = VoteType::Postal;
        if (boothName == "Absent Votes") voteType = VoteType::Absent;
        if (boothName == "Early Votes") voteType = VoteType::Early;
        if (boothName == "Provisional Votes") voteType = VoteType::Provisional;
        if (voteType == VoteType::Invalid) continue;
        auto fps = boothValue["fp"];
        for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
          int fpCandIndexI = std::stoi(fpCandIndex);
          int fpCandId = indexToId[fpCandIndexI];
          seats[seatId].fpVotes[fpCandId][voteType] += fpVotes;
        }
        auto tcps = boothValue["tcp"];
        for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
          int tcpCandIndexI = std::stoi(tcpCandIndex);
          int tcpCandId = indexToId[tcpCandIndexI];
          int tcpAffiliation = candidates[tcpCandId].party;
          seats[seatId].tcpVotes[tcpAffiliation][voteType] += tcpVotes;
        }
        continue;
      }
      int boothId = dummyBoothId;
      if (boothNameToId.contains(boothName)) {
        boothId = boothNameToId[boothName];
      }
      else if (ambiguousBoothNameToId.contains({ boothName, seatName })) {
        boothId = ambiguousBoothNameToId[{ boothName, seatName }];
      }
      else {
        Booth booth;
        booth.name = boothName;
        booth.id = dummyBoothId;
        booths[booth.id] = booth;
        --dummyBoothId;
      }
      if (matchedIds.contains(boothId)) {
        // If two "old" booths match to one "new" booth then we don't know
        // which "old" booth to actually compare to
        // so ... make new booths that won't match to either
        if (booths.contains(boothId)) {
          auto boothData = booths[boothId];
          boothData.id = dummyBoothId;
          booths.erase(boothId);
          booths[dummyBoothId] = boothData;
          auto& parent = seats[booths[dummyBoothId].parentSeat];
          //PA_LOG_VAR(parent.name);
          //PA_LOG_VAR(parent.booths);
          //PA_LOG_VAR(boothId);
          //PA_LOG_VAR(dummyBoothId);
          auto foundBoothId = std::find(parent.booths.begin(), parent.booths.end(), boothId);
          if (foundBoothId != parent.booths.end()) *foundBoothId = dummyBoothId;
          //PA_LOG_VAR(parent.booths);
          //logger << "Detached old ambiguous booth: " << booths[dummyBoothId].name << "\n";
          --dummyBoothId;
        }
        boothId = dummyBoothId;
        //logger << "Detached ambiguous booth: " << boothName << "\n";
        --dummyBoothId;
      }
      matchedIds.insert(boothId);
      Booth& booth = booths[boothId];
      // the parent seat refers to the seat this booth is in for the new election,
      // but it should the the one it is in for this (old) election
      booth.parentSeat = seatId;
      booth.id = boothId;
      booth.name = boothName;
      auto fps = boothValue["fp"];
      for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
        int fpCandIndexI = std::stoi(fpCandIndex);
        int fpCandId = indexToId[fpCandIndexI];
        booth.fpVotes[fpCandId] = fpVotes;
        if (seatId > 0) seats[seatId].fpVotes[fpCandId][VoteType::Ordinary] += fpVotes;
      }
      auto tcps = boothValue["tcp"];
      for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
        int tcpCandIndexI = std::stoi(tcpCandIndex);
        int tcpCandId = indexToId[tcpCandIndexI];
        int tcpAffiliation = candidates[tcpCandId].party;
        booth.tcpVotes[tcpAffiliation] = tcpVotes;
        if (seatId > 0) seats[seatId].tcpVotes[tcpAffiliation][VoteType::Ordinary] += tcpVotes;
      }
      if (seatId > 0) {
        seats[seatId].booths.push_back(booth.id);
      }
    }
  }

  //logger << "==SEATS==\n";
  //for (auto const& [seatId, seat] : seats) {
  //	PA_LOG_VAR(seat.name);
  //	PA_LOG_VAR(seat.id);
  //	PA_LOG_VAR(seat.enrolment);
  //	PA_LOG_VAR(seat.fpVotes);
  //	PA_LOG_VAR(seat.tcpVotes);
  //	PA_LOG_VAR(seat.tppVotes);
  //	for (int boothId : seat.booths) {
  //		PA_LOG_VAR(boothId);
  //		auto const& booth = booths.at(boothId);
  //		PA_LOG_VAR(booth.id);
  //		PA_LOG_VAR(booth.name);
  //		PA_LOG_VAR(booth.fpVotes);
  //		PA_LOG_VAR(booth.tcpVotes);
  //		PA_LOG_VAR(booth.type);
  //	}
  //}
  //logger << "==CANDIDATES==\n";
  //for (auto const& candidate : candidates) {
  //	PA_LOG_VAR(candidate.second.id);
  //	PA_LOG_VAR(candidate.second.name);
  //	if (candidate.second.party != -1) {
  //		PA_LOG_VAR(parties[candidate.second.party].name);
  //	}
  //	else {
  //		logger << "Independent\n";
  //	}
  //}
  //logger << "==PARTIES==\n";
  //for (auto const& party : parties) {
  //	PA_LOG_VAR(party.second.id);
  //	PA_LOG_VAR(party.second.name);
  //	PA_LOG_VAR(party.second.shortCode);
  //}
  logger << "Appeared to successfully load past election data\n";
}

void Results2::Election::preloadNswec([[maybe_unused]] nlohmann::json const& results, tinyxml2::XMLDocument const& zeros, bool includeSeatBooths)
{
  const std::map<std::string, std::string> shortCodes = {
    {"Shooters, Fishers and Farmers Party (NSW) Incorporated", "SFF"},
    {"The Greens NSW", "GRN"},
    {"Australian Labor Party (NSW Branch)", "ALP"},
    {"Country Labor Party", "ALP"},
    {"The Liberal Party of Australia, New South Wales Division", "LNP"},
    {"National Party of Australia - NSW", "LNP"}
  };
  auto const& mediaFeed = requiredChild(zeros, "MediaFeed", "NSWEC preload");
  auto const& election = requiredChild(mediaFeed, "Election", "NSWEC preload/MediaFeed");
  auto const& house = requiredChild(election, "House", "NSWEC preload/MediaFeed/Election");
  auto const& contests = requiredChild(house, "Contests", "NSWEC preload/MediaFeed/Election/House");
  auto currentContest = contests.FirstChildElement("Contest");
  if (!currentContest) throwInvalidXml("NSWEC preload/MediaFeed/Election/House/Contests", "missing <Contest>");
  int seatIdCounter = 1;
  int candidateIdCounter = 1;
  std::map<std::string, std::vector<int>> candidateIdsByName;
  while (currentContest) {
    Seat seat;
    auto const& seatIdentifier = requiredChild(*currentContest,
      "PollingDistrictIdentifier", "NSWEC preload/Contest");
    seat.name = requiredAttribute(seatIdentifier, "Id", "NSWEC preload/Contest/PollingDistrictIdentifier");
    seat.id = seatIdCounter;
    ++seatIdCounter;
    if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
    auto const seatContext = "NSWEC preload/Contest[" + seat.name + "]";
    seat.enrolment = requiredIntText(
      requiredChild(*currentContest, "Enrolment", seatContext),
      seatContext + "/Enrolment", 0);
    auto const& firstPreferences = requiredChild(*currentContest, "FirstPreferences", seatContext);
    auto currentCandidate = firstPreferences.FirstChildElement("Candidate");
    while (currentCandidate) {
      Candidate candidate;
      auto const& candidateIdentifier = requiredChild(*currentCandidate,
        "CandidateIdentifier", seatContext + "/FirstPreferences/Candidate");
      candidate.name = requiredAttribute(candidateIdentifier, "Id",
        seatContext + "/FirstPreferences/Candidate/CandidateIdentifier");
      candidate.id = candidateIdCounter;
      ++candidateIdCounter;
      auto const* affiliationEl = currentCandidate->FirstChildElement("Affiliation");
      auto const* affiliationIdentifier = affiliationEl ?
        affiliationEl->FirstChildElement("AffiliationIdentifier") : nullptr;
      auto const* affiliationIdEl = affiliationIdentifier ?
        affiliationIdentifier->FindAttribute("Id") : nullptr;
      if (affiliationIdEl && *affiliationIdEl->Value()) {
        candidate.party = requiredIntAttribute(*affiliationIdentifier, "Id",
          seatContext + "/FirstPreferences/Candidate/Affiliation/AffiliationIdentifier", 0);
        if (!parties.contains(candidate.party)) {
          parties[candidate.party] = Party();
          parties[candidate.party].id = candidate.party;
          parties[candidate.party].name = requiredText(
            requiredChild(*affiliationIdentifier, "RegisteredName",
              seatContext + "/FirstPreferences/Candidate/Affiliation/AffiliationIdentifier"),
            seatContext + "/FirstPreferences/Candidate/Affiliation/AffiliationIdentifier/RegisteredName");
          if (shortCodes.contains(parties[candidate.party].name)) {
            parties[candidate.party].shortCode = shortCodes.at(parties[candidate.party].name);
          }
        }
      }
      else {
        candidate.party = Candidate::Independent;
        parties[Candidate::Independent].id = Candidate::Independent;
        parties[Candidate::Independent].shortCode = "IND";
        parties[Candidate::Independent].name = "Independent";
      }
      candidates[candidate.id] = candidate;
      candidateNameToId[nswecCandidateKey(seat.id, candidate.name)] = candidate.id;
      candidateIdsByName[candidate.name].push_back(candidate.id);

      currentCandidate = currentCandidate->NextSiblingElement("Candidate");
    }

    auto const& pollingPlaces = requiredChild(*currentContest, "PollingPlaces", seatContext);
    auto currentBooth = pollingPlaces.FirstChildElement("PollingPlace");
    while (currentBooth) {
      Booth booth;
      auto const& boothIdentifier = requiredChild(*currentBooth,
        "PollingPlaceIdentifier", seatContext + "/PollingPlaces/PollingPlace");
      booth.name = requiredAttribute(boothIdentifier, "Name",
        seatContext + "/PollingPlaces/PollingPlace/PollingPlaceIdentifier");
      if (booth.name == "Absent" || booth.name == "Enrolment/Provisional" ||
        booth.name == "Postal" || booth.name == "iVote") {
        currentBooth = currentBooth->NextSiblingElement("PollingPlace");
        continue;
      }
      booth.id = requiredIntAttribute(boothIdentifier, "Id",
        seatContext + "/PollingPlaces/PollingPlace/PollingPlaceIdentifier", 0);
      booth.id += seat.id * 100000; // create unique booth ID for booths with the same name in different seats
      booth.parentSeat = seat.id;
      booths[booth.id] = booth;
      currentBooth = currentBooth->NextSiblingElement("PollingPlace");
      if (includeSeatBooths) {
        seat.booths.push_back(booth.id);
      }
    }

    seats[seat.id] = seat;
    currentContest = currentContest->NextSiblingElement("Contest");
  }
  if (!results.is_null()) {
    // A lot of this is just a straight up copy of the Victorian Election
    // In summary, the purpose is to match past booth data to current booth data
    // The length of the code is primarily to deal with various types of ambiguities
    // that can result from booths with the same name under redistributions
    // (and can therefore take on vastly different character depening on the
    // boundary changes) so these are largely removed unless the booth
    // changed cleanly from one seat to another.
    std::map<std::string, int> seatNameToId;
    for (auto [seatId, seat] : seats) {
      if (seatNameToId.contains(seat.name)) {
        logger << "Warning: duplicate name for " << seat.name << "!\n";
      }
      else {
        seatNameToId[seat.name] = seatId;
      }
    }
    std::set<std::string> seenBooths;
    std::set<std::string> ambiguousBooths;
    for (auto [boothId, booth] : booths) {
      if (seenBooths.contains(booth.name)) {
        ambiguousBooths.emplace(booth.name);
      }
      else {
        seenBooths.emplace(booth.name);
      }
    }
    std::map<std::string, int> boothNameToId;
    std::map<std::pair<std::string, std::string>, int> ambiguousBoothNameToId;
    for (auto [boothId, booth] : booths) {
      if (ambiguousBooths.contains(booth.name)) {
        ambiguousBoothNameToId[{booth.name, seats[booth.parentSeat].name}] = boothId;
      }
      else {
        boothNameToId[booth.name] = boothId;
      }
    }
    //PA_LOG_VAR(boothNameToId);
    //PA_LOG_VAR(ambiguousBoothNameToId);
    int dummyCandidateId = -100000; // Low numbers that will never be mistaken for an official id
    int dummyBoothId = -100000; // Low numbers that will never be mistaken for an official id
    // Not worth soft-coding this
    const std::map<std::string, int> partyIds = {
      {"LAB", 2},
      {"CDP", 4},
      {"LIB", 7},
      {"GRN", 16},
      {"CLP", 5},
      {"NAT", 8},
      {"NP", 8},
      {"SFF", 17},
      {"SA", 14},
      {"KSO", 979},
      {"SAP", 857},
      {"AJP", 376},
      {"LDP", 310},
      {"AC", 837},
      {"PHON", 937},
      {"SBP", 957},
      {"UP", -1},
      {"ORP", -1},
      {"NLT", -1},
      {"ACP", -1},
      {"FLUX", -1},
      {"IND", -1}
    };
    std::set<int> matchedIds;
    for (auto const& [seatName, seatValue] : results.items()) {
      int seatId = -1;
      if (seatNameToId.contains(seatName)) {
        seatId = seatNameToId[seatName];
      }
      std::map<int, int> indexToId;
      for (auto const& [candIndex, candValue] : seatValue["candidates"].items()) {
        int candIndexI = std::stoi(candIndex);
        auto candidateName = candValue["name"];
        std::string const candidateNameText = candidateName;
        auto party = candValue["party"];
        // Historical JSON stores the full ballot name. Prefer an exact match
        // in the same seat, then a unique exact match across the election to
        // accommodate redistributions. Ambiguous/unmatched candidates receive
        // synthetic IDs below.
        auto const candidateKey = nswecCandidateKey(seatId, candidateNameText);
        if (seatId > 0 && candidateNameToId.contains(candidateKey)) {
          indexToId[candIndexI] = candidateNameToId[candidateKey];
        }
        else if (candidateIdsByName.contains(candidateNameText) &&
          candidateIdsByName.at(candidateNameText).size() == 1) {
          indexToId[candIndexI] = candidateIdsByName.at(candidateNameText).front();
        }
        else {
          indexToId[candIndexI] = dummyCandidateId;
          Candidate candidate;
          candidate.id = dummyCandidateId;
          candidate.name = candidateNameText;
          if (!partyIds.contains(party)) PA_LOG_VAR(seatName);
          if (!partyIds.contains(party)) PA_LOG_VAR(party);
          candidate.party = partyIds.at(party);
          candidates[candidate.id] = candidate;
          --dummyCandidateId;
        }
      }
      for (auto const& [boothName, boothValue] : seatValue["booths"].items()) {
        if (boothName.find("Votes") != std::string::npos) {
          VoteType voteType = VoteType::Invalid;
          if (boothName == "Postal Votes") voteType = VoteType::Postal;
          if (boothName == "Absent Votes") voteType = VoteType::Absent;
          if (boothName == "Provisional Votes") voteType = VoteType::Provisional;
          if (boothName == "iVote Votes") voteType = VoteType::IVote;
          if (voteType == VoteType::Invalid) continue;
          auto fps = boothValue["fp"];
          for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
            int fpCandIndexI = std::stoi(fpCandIndex);
            int fpCandId = indexToId[fpCandIndexI];
            seats[seatId].fpVotes[fpCandId][voteType] += fpVotes;
          }
          auto tcps = boothValue["tcp"];
          for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
            int tcpCandIndexI = std::stoi(tcpCandIndex);
            int tcpCandId = indexToId[tcpCandIndexI];
            int tcpAffiliation = candidates[tcpCandId].party;
            seats[seatId].tcpVotes[tcpAffiliation][voteType] += tcpVotes;
            seats[seatId].tcpVotesCandidate[tcpCandId][voteType] += tcpVotes;
          }
          continue;
        }
        int boothId = dummyBoothId;
        if (boothNameToId.contains(boothName)) {
          boothId = boothNameToId[boothName];
        }
        else if (ambiguousBoothNameToId.contains({ boothName, seatName })) {
          boothId = ambiguousBoothNameToId[{ boothName, seatName }];
        }
        else {
          Booth booth;
          booth.name = boothName;
          booth.id = dummyBoothId;
          booths[booth.id] = booth;
          --dummyBoothId;
        }
        if (matchedIds.contains(boothId)) {
          // If two "old" booths match to one "new" booth then we don't know
          // which "old" booth to actually compare to
          // so ... make new booths that won't match to either
          if (booths.contains(boothId)) {
            auto boothData = booths[boothId];
            boothData.id = dummyBoothId;
            booths.erase(boothId);
            booths[dummyBoothId] = boothData;
            auto& parent = seats[booths[dummyBoothId].parentSeat];
            //PA_LOG_VAR(parent.name);
            //PA_LOG_VAR(parent.booths);
            //PA_LOG_VAR(boothId);
            //PA_LOG_VAR(dummyBoothId);
            auto foundBoothId = std::find(parent.booths.begin(), parent.booths.end(), boothId);
            if (foundBoothId != parent.booths.end()) *foundBoothId = dummyBoothId;
            //PA_LOG_VAR(parent.booths);
            //logger << "Detached old ambiguous booth: " << booths[dummyBoothId].name << "\n";
            --dummyBoothId;
          }
          boothId = dummyBoothId;
          //logger << "Detached ambiguous booth: " << boothName << "\n";
          --dummyBoothId;
        }
        matchedIds.insert(boothId);
        Booth& booth = booths[boothId];
        // the parent seat refers to the seat this booth is in for the new election,
        // but it should be the one it is in for this (old) election
        booth.parentSeat = seatId;
        booth.id = boothId;
        booth.name = boothName;
        auto fps = boothValue["fp"];
        for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
          int fpCandIndexI = std::stoi(fpCandIndex);
          int fpCandId = indexToId[fpCandIndexI];
          booth.fpVotes[fpCandId] = fpVotes;
          if (seatId > 0) seats[seatId].fpVotes[fpCandId][VoteType::Ordinary] += fpVotes;
        }
        auto tcps = boothValue["tcp"];
        for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
          int tcpCandIndexI = std::stoi(tcpCandIndex);
          int tcpCandId = indexToId[tcpCandIndexI];
          int tcpAffiliation = candidates[tcpCandId].party;
          booth.tcpVotes[tcpAffiliation] = tcpVotes;
          if (seatId > 0) seats[seatId].tcpVotes[tcpAffiliation][VoteType::Ordinary] += tcpVotes;
        }
        auto tpps = boothValue["tpp"];
        for (auto const& [tppCandIndex, tppVotes] : tpps.items()) {
          int tppCandIndexI = std::stoi(tppCandIndex);
          int tppCandId = indexToId[tppCandIndexI];
          int tppAffiliation = candidates[tppCandId].party;
          booth.tppVotes[tppAffiliation] = tppVotes;
          if (seatId > 0) seats[seatId].tppVotes[tppAffiliation][VoteType::Ordinary] += tppVotes;
        }
        if (seatId > 0) {
          seats[seatId].booths.push_back(booth.id);
        }
      }
    }

    // logger << "==SEATS==\n";
    // for (auto const& [seatId, seat] : seats) {
    // 	PA_LOG_VAR(seat.name);
    // 	PA_LOG_VAR(seat.id);
    // 	PA_LOG_VAR(seat.enrolment);
    // 	PA_LOG_VAR(seat.fpVotes);
    // 	PA_LOG_VAR(seat.tcpVotes);
    // 	PA_LOG_VAR(seat.tppVotes);
    // 	for (int boothId : seat.booths) {
    // 		PA_LOG_VAR(boothId);
    // 		auto const& booth = booths.at(boothId);
    // 		PA_LOG_VAR(booth.id);
    // 		PA_LOG_VAR(booth.name);
    // 		PA_LOG_VAR(booth.fpVotes);
    // 		PA_LOG_VAR(booth.tcpVotes);
    // 		PA_LOG_VAR(booth.type);
    // 	}
    // }
    // logger << "==CANDIDATES==\n";
    // for (auto const& candidate : candidates) {
    // 	PA_LOG_VAR(candidate.second.id);
    // 	PA_LOG_VAR(candidate.second.name);
    // 	if (candidate.second.party != -1) {
    // 		PA_LOG_VAR(parties[candidate.second.party].name);
    // 	}
    // 	else {
    // 		logger << "Independent\n";
    // 	}
    // }
    // logger << "==PARTIES==\n";
    // for (auto const& party : parties) {
    // 	PA_LOG_VAR(party.second.id);
    // 	PA_LOG_VAR(party.second.name);
    // 	PA_LOG_VAR(party.second.shortCode);
    // }
    logger << "Appeared to successfully load past election data\n";
  }
}

void Results2::Election::preloadQec([[maybe_unused]] nlohmann::json const& results, tinyxml2::XMLDocument const& zeros)
{
  const std::map<std::string, std::string> shortCodes = {
    {"Queensland Greens", "GRN"},
    {"Australian Labor Party (State of Queensland)", "ALP"},
    {"Liberal National Party of Queensland", "LNP"},
    {"Pauline Hanson's One Nation Queensland Division", "ON"},
    {"Katter's Australian Party (KAP)", "KAP"},
    {"Family First Queensland", "FF"},
    {"Independent", "IND"}
  };
  auto const& ecq = requiredChild(zeros, "ecq", "QEC preload");
  auto const& election = requiredChild(ecq, "election", "QEC preload/ecq");
  auto const& districts = requiredChild(election, "districts", "QEC preload/ecq/election");
  auto currentDistrict = districts.FirstChildElement("district");
  if (!currentDistrict) throwInvalidXml("QEC preload/ecq/election/districts", "missing <district>");
  int candidateIdCounter = 1;
  int partyIdCounter = 1;
  std::map<std::string, int> partyNameToPartyId;
  partyNameToPartyId["Independent"] = -1;
  while (currentDistrict) {
    Seat seat;
    seat.name = requiredAttribute(*currentDistrict, "districtName", "QEC preload/district");
    seat.id = requiredIntAttribute(*currentDistrict, "number", "QEC preload/district", 0);
    if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
    auto const seatContext = "QEC preload/district[" + seat.name + "]";
    seat.enrolment = requiredIntAttribute(*currentDistrict, "enrolment", seatContext, 0);
    auto const& candidateList = requiredChild(*currentDistrict, "candidates", seatContext);
    auto currentCandidate = candidateList.FirstChildElement("candidate");
    if (!currentCandidate) throwInvalidXml(seatContext + "/candidates", "missing <candidate>");
    while (currentCandidate) {
      Candidate candidate;
      candidate.name = requiredAttribute(*currentCandidate, "ballotName",
        seatContext + "/candidates/candidate");
      candidate.id = candidateIdCounter;
      ++candidateIdCounter;
      auto const partyName = requiredAttribute(*currentCandidate, "party",
        seatContext + "/candidates/candidate");
      if (!partyNameToPartyId.contains(partyName)) {
        partyNameToPartyId[partyName] = partyIdCounter;
        ++partyIdCounter;
      }
      candidate.party = partyNameToPartyId[partyName];
      if (!parties.contains(candidate.party)) {
        parties[candidate.party] = Party();
        parties[candidate.party].id = candidate.party;
        parties[candidate.party].name = partyName;
        if (shortCodes.contains(parties[candidate.party].name)) {
          parties[candidate.party].shortCode = shortCodes.at(parties[candidate.party].name);
        }
      }
      candidates[candidate.id] = candidate;
      if (candidateNameToId.contains(candidate.name)) {
        logger << "WARNING: Identical candidate names found: " << candidate.name << "\n";
      }
      candidateNameToId[candidate.name] = candidate.id;
      candidateNameToId[qecCandidateKey(seat.id, candidate.name)] = candidate.id;

      currentCandidate = currentCandidate->NextSiblingElement("candidate");
    }

    auto const& countRound = requiredChild(*currentDistrict, "countRound", seatContext);
    auto const& boothList = requiredChild(countRound, "booths", seatContext + "/countRound");
    auto currentBooth = boothList.FirstChildElement("booth");
    while (currentBooth) {
      Booth booth;
      booth.id = requiredIntAttribute(*currentBooth, "id",
        seatContext + "/countRound/booths/booth", 0);
      booth.id += seat.id * 100000; // create unique booth ID for booths with the same name in different seats
      booth.name = requiredAttribute(*currentBooth, "name",
        seatContext + "/countRound/booths/booth");
      if (booth.name.find("Returning Officer") != std::string::npos ||
        booth.name.find("Early Voting Centre") != std::string::npos) {
        booth.type = Booth::Type::Ppvc;
      }
      booth.parentSeat = seat.id;
      booths[booth.id] = booth;
      currentBooth = currentBooth->NextSiblingElement("booth");
      if (results.is_null()) {
        // If there are no results - we're doing the current election,
        // we won't need to do a booth alignment so adding booths to seats
        // need to happen here
        seat.booths.push_back(booth.id);
      }
    }

    seats[seat.id] = seat;
    currentDistrict = currentDistrict->NextSiblingElement("district");
  }

  if (!results.is_null()) {
    // A lot of this is just a straight up copy of the Victorian Election
    // In summary, the purpose is to match past booth data to currents booth data
    // The length of the code is primarily to deal with various types of ambiguities
    // that can result from booths with the same name under redistributions
    // (and can therefore take on vastly different character depending on the
    // boundary changes) so these are largely removed unless the booth
    // changed cleanly from one seat to another.
    std::map<std::string, int> seatNameToId;
    for (auto [seatId, seat] : seats) {
      if (seatNameToId.contains(seat.name)) {
        logger << "Warning: duplicate name for " << seat.name << "!\n";
      }
      else {
        seatNameToId[seat.name] = seatId;
      }
    }
    std::set<std::string> seenBooths;
    std::set<std::string> ambiguousBooths;
    for (auto [boothId, booth] : booths) {
      if (seenBooths.contains(booth.name)) {
        ambiguousBooths.emplace(booth.name);
      }
      else {
        seenBooths.emplace(booth.name);
      }
    }
    std::map<std::string, int> boothNameToId;
    std::map<std::pair<std::string, std::string>, int> ambiguousBoothNameToId;
    for (auto [boothId, booth] : booths) {
      if (ambiguousBooths.contains(booth.name)) {
        ambiguousBoothNameToId[{booth.name, seats[booth.parentSeat].name}] = boothId;
      }
      else {
        boothNameToId[booth.name] = boothId;
      }
    }
    int dummyCandidateId = -100000; // Low numbers that will never be mistaken for an official id
    int dummyBoothId = -100000; // Low numbers that will never be mistaken for an official id
    //PA_LOG_VAR(boothNameToId);
    //PA_LOG_VAR(ambiguousBoothNameToId);
    std::set<int> matchedIds;
    for (auto const& [seatName, seatValue] : results.items()) {
      int seatId = -1;
      if (seatNameToId.contains(seatName)) {
        seatId = seatNameToId[seatName];
      }
      std::map<int, int> indexToId;
      for (auto const& [candIndex, candValue] : seatValue["candidates"].items()) {
        int candIndexI = std::stoi(candIndex);
        auto candidateName = candValue["name"];
        auto party = candValue["party"];
        auto const candidateKey = qecCandidateKey(seatId, std::string(candidateName));
        if (candidateNameToId.contains(candidateKey)) {
          indexToId[candIndexI] = candidateNameToId[candidateKey];
        }
        else {
          indexToId[candIndexI] = dummyCandidateId;
          Candidate candidate;
          candidate.id = dummyCandidateId;
          candidate.name = candidateName;
          if (partyNameToPartyId.contains(party)) {
            candidate.party = partyNameToPartyId[party];
          }
          else {
            candidate.party = -1;
          }
          candidates[candidate.id] = candidate;
          --dummyCandidateId;
        }
      }
      for (auto const& [boothName, boothValue] : seatValue["booths"].items()) {
        VoteType voteType = VoteType::Ordinary;
        if (boothName == "Absent Early Voting") voteType = VoteType::Absent;
        if (boothName == "Absent Election Day") voteType = VoteType::Absent;
        if (boothName == "Postal Declaration Votes") voteType = VoteType::Postal;
        if (boothName == "In Person Declaration Votes") voteType = VoteType::Provisional;
        if (boothName == "Telephone Voting") voteType = VoteType::Telephone;
        if (boothName == "Telephone Voting - Early Voting") voteType = VoteType::Telephone;
        if (voteType != VoteType::Ordinary) {
          auto fps = boothValue["fp"];
          for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
            int fpCandIndexI = std::stoi(fpCandIndex);
            int fpCandId = indexToId[fpCandIndexI];
            seats[seatId].fpVotes[fpCandId][voteType] += fpVotes;
          }
          auto tcps = boothValue["tcp"];
          for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
            int tcpCandIndexI = std::stoi(tcpCandIndex);
            int tcpCandId = indexToId[tcpCandIndexI];
            int tcpAffiliation = candidates[tcpCandId].party;
            seats[seatId].tcpVotes[tcpAffiliation][voteType] += tcpVotes;
            seats[seatId].tcpVotesCandidate[tcpCandId][voteType] += tcpVotes;
          }
          continue;
        }
        int boothId = dummyBoothId;
        if (boothNameToId.contains(boothName)) {
          boothId = boothNameToId[boothName];
        }
        else if (ambiguousBoothNameToId.contains({ boothName, seatName })) {
          boothId = ambiguousBoothNameToId[{ boothName, seatName }];
        }
        else {
          Booth booth;
          booth.name = boothName;
          booth.id = dummyBoothId;
          booths[booth.id] = booth;
          --dummyBoothId;
        }
        if (matchedIds.contains(boothId)) {
          // If two "old" booths match to one "new" booth then we don't know
          // which "old" booth to actually compare to
          // so ... make new booths that won't match to either
          if (booths.contains(boothId)) {
            auto boothData = booths[boothId];
            boothData.id = dummyBoothId;
            booths.erase(boothId);
            booths[dummyBoothId] = boothData;
            auto& parent = seats[booths[dummyBoothId].parentSeat];
            //PA_LOG_VAR(parent.name);
            //PA_LOG_VAR(parent.booths);
            //PA_LOG_VAR(boothId);
            //PA_LOG_VAR(dummyBoothId);
            auto foundBoothId = std::find(parent.booths.begin(), parent.booths.end(), boothId);
            if (foundBoothId != parent.booths.end()) *foundBoothId = dummyBoothId;
            //PA_LOG_VAR(parent.booths);
            logger << "Detached old ambiguous booth: " << booths[dummyBoothId].name << "\n";
            --dummyBoothId;
          }
          boothId = dummyBoothId;
          logger << "Detached ambiguous booth: " << boothName << "\n";
          --dummyBoothId;
        }
        matchedIds.insert(boothId);
        Booth& booth = booths[boothId];
        // the parent seat refers to the seat this booth is in for the new election,
        // but it should be the one it is in for this (old) election
        booth.parentSeat = seatId;
        booth.id = boothId;
        booth.name = boothName;
        auto fps = boothValue["fp"];
        for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
          int fpCandIndexI = std::stoi(fpCandIndex);
          int fpCandId = indexToId[fpCandIndexI];
          booth.fpVotes[fpCandId] = fpVotes;
          if (seatId > 0) seats[seatId].fpVotes[fpCandId][VoteType::Ordinary] += fpVotes;
        }
        auto tcps = boothValue["tcp"];
        for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
          int tcpCandIndexI = std::stoi(tcpCandIndex);
          int tcpCandId = indexToId[tcpCandIndexI];
          int tcpAffiliation = candidates[tcpCandId].party;
          booth.tcpVotes[tcpAffiliation] = tcpVotes;
          booth.tcpVotesCandidate[tcpCandId] = tcpVotes;
          if (seatId > 0) seats[seatId].tcpVotes[tcpAffiliation][VoteType::Ordinary] += tcpVotes;
          if (seatId > 0) seats[seatId].tcpVotesCandidate[tcpCandId][VoteType::Ordinary] += tcpVotes;
        }
        if (seatId > 0) {
          seats[seatId].booths.push_back(booth.id);
        }
      }
    }
  }

  // logger << "Qec Preload\n";
  // PA_LOG_VAR(booths.size());
  // logger << "==SEATS==\n";
  // for (auto const& [seatId, seat] : seats) {
  // 	PA_LOG_VAR(seat.name);
  // 	PA_LOG_VAR(seat.id);
  // 	PA_LOG_VAR(seat.enrolment);
  // 	PA_LOG_VAR(seat.fpVotes);
  // 	PA_LOG_VAR(seat.tcpVotes);
  // 	PA_LOG_VAR(seat.tppVotes);
  // 	PA_LOG_VAR(seat.booths);
  // 	for (int boothId : seat.booths) {
  // 		PA_LOG_VAR(boothId);
  // 		auto const& booth = booths.at(boothId);
  // 		PA_LOG_VAR(booth.id);
  // 		PA_LOG_VAR(booth.name);
  // 		PA_LOG_VAR(booth.fpVotes);
  // 		PA_LOG_VAR(booth.tcpVotes);
  // 		PA_LOG_VAR(booth.type);
  // 	}
  // }
  // logger << "==CANDIDATES==\n";
  // for (auto const& candidate : candidates) {
  // 	PA_LOG_VAR(candidate.second.id);
  // 	PA_LOG_VAR(candidate.second.name);
  // 	if (candidate.second.party != -1) {
  // 		PA_LOG_VAR(parties[candidate.second.party].name);
  // 	}
  // 	else {
  // 		logger << "Independent\n";
  // 	}
  // }
  // logger << "==PARTIES==\n";
  // for (auto const& party : parties) {
  // 	PA_LOG_VAR(party.second.id);
  // 	PA_LOG_VAR(party.second.name);
  // 	PA_LOG_VAR(party.second.shortCode);
  // }
  logger << "Appeared to successfully load past election data\n";
}

void Results2::Election::preloadWaec(tinyxml2::XMLDocument const& candidatesXml, tinyxml2::XMLDocument const& boothsXml)
{
  auto currentRegion = boothsXml.FirstChildElement()->FirstChildElement("ElectionRegion");
  while (currentRegion) {

    auto currentDistrict = currentRegion->FirstChildElement("ElectionDistrict");
    while (currentDistrict) {

      Seat seat;
      seat.name = currentDistrict->Attribute("Name");
      seat.id = hashName(seat.name);
      if (seats.contains(seat.id)) seat = seats[seat.id];
      seat.enrolment = currentDistrict->IntAttribute("Enrolment");

      auto currentBooth = currentDistrict->FirstChildElement("OrdinaryPollingPlace");
      while (currentBooth) {
        Booth booth;
        booth.id = hashName(currentBooth->Attribute("VenueId"));
        if (booths.contains(booth.id)) booth = booths[booth.id];
        booth.name = currentBooth->Attribute("Name");
        booth.parentSeat = seat.id;
        booths[booth.id] = booth;
        seat.booths.push_back(booth.id);

        currentBooth = currentBooth->NextSiblingElement("OrdinaryPollingPlace");
      }

      seats[seat.id] = seat;
      currentDistrict = currentDistrict->NextSiblingElement("ElectionDistrict");
    }

    currentRegion = currentRegion->NextSiblingElement("ElectionRegion");
  }

  int candidateIdCounter = 1;
  int partyIdCounter = 1;
  std::map<std::string, int> partyShortCodeToPartyId;

  currentRegion = candidatesXml.FirstChildElement()->FirstChildElement("ElectionRegion");
  while (currentRegion) {
    auto currentDistrict = currentRegion->FirstChildElement("ElectionDistrict");
    while (currentDistrict) {
      auto seatId = hashName(currentDistrict->Attribute("Name"));
      Seat& seat = seats.at(seatId);

      auto currentCandidate = currentDistrict->FirstChildElement("LA")->FirstChildElement("Candidate");
      while (currentCandidate) {
        Candidate candidate;
        candidate.name = currentCandidate->Attribute("BallotPaperName");
        candidate.id = candidateIdCounter;
        ++candidateIdCounter;
        auto partyShortCode = currentCandidate->Attribute("RegisteredPartyAbbreviation");
        if (!partyShortCodeToPartyId.contains(partyShortCode)) {
          partyShortCodeToPartyId[partyShortCode] = partyIdCounter;
          ++partyIdCounter;
        }
        candidate.party = partyShortCodeToPartyId[partyShortCode];
        if (!parties.contains(candidate.party)) {
          parties[candidate.party] = Party();
          parties[candidate.party].id = candidate.party;
          parties[candidate.party].name = currentCandidate->Attribute("RegisteredPartyBallotPaperName");
          parties[candidate.party].shortCode = partyShortCode;
        }
        candidates[candidate.id] = candidate;
        if (candidateNameToId.contains(candidate.name)) {
          // If this triggers, we'll just need to add the seat name to the candidate name
          logger << "WARNING: Identical candidate names found: " << candidate.name << "\n";
        }
        candidateNameToId[candidate.name] = candidate.id;

        // Initialise the votes for this candidate in all booths in the seat
        for (auto const& boothId : seat.booths) {
          booths[boothId].fpVotes[candidate.id] = 0;
        }
      
        currentCandidate = currentCandidate->NextSiblingElement("Candidate");
      }

      currentDistrict = currentDistrict->NextSiblingElement("ElectionDistrict");
    }

    currentRegion = currentRegion->NextSiblingElement("ElectionRegion");
  }

  // logger << "Waec Preload\n";
  // PA_LOG_VAR(booths.size());
  // logger << "==SEATS==\n";
  // for (auto const& [seatId, seat] : seats) {
  // 	PA_LOG_VAR(seat.name);
  // 	PA_LOG_VAR(seat.id);
  // 	PA_LOG_VAR(seat.enrolment);
  // 	PA_LOG_VAR(seat.booths);
  // 	for (int boothId : seat.booths) {
  // 		PA_LOG_VAR(boothId);
  // 		auto const& booth = booths.at(boothId);
  // 		PA_LOG_VAR(booth.id);
  // 		PA_LOG_VAR(booth.name);
  // 		PA_LOG_VAR(booth.type);
  // 	}
  // }
  // logger << "==CANDIDATES==\n";
  // for (auto const& candidate : candidates) {
  // 	PA_LOG_VAR(candidate.second.id);
  // 	PA_LOG_VAR(candidate.second.name);
  // 	if (candidate.second.party != -1) {
  // 		PA_LOG_VAR(parties[candidate.second.party].name);
  // 	}
  // 	else {
  // 		logger << "Independent\n";
  // 	}
  // }
  // logger << "==PARTIES==\n";
  // for (auto const& party : parties) {
  // 	PA_LOG_VAR(party.second.id);
  // 	PA_LOG_VAR(party.second.name);
  // 	PA_LOG_VAR(party.second.shortCode);
  // }
  logger << "Appeared to successfully load past election data\n";
}



Results2::VoteType getBoothVoteTypeEcsa(std::string boothName) {
  if (boothName == "Early Voting Absent Ordinary Votes") return Results2::VoteType::PrePoll;
  if (boothName == "Polling Day Absent Ordinary Votes") return Results2::VoteType::Absent;
  if (boothName == "Postal Votes") return Results2::VoteType::Postal;
  if (boothName == "Polling Day Declaration Votes") return Results2::VoteType::Provisional;
  if (boothName == "Early Voting Declaration Votes") return Results2::VoteType::EarlyProvisional;
  if (boothName == "Electoral Visitor/Mobile Declaration Votes") return Results2::VoteType::EVM;
  if (boothName == "Telephone/Interstate/Overseas Declaration Votes") return Results2::VoteType::TIO;
  return Results2::VoteType::Ordinary;
};

int generateBoothIdEcsa(int seatId, std::string boothName) {
	// Booths with ID of zero are declaration/absent votes
	// create a unique ID for this booth
	return seatId * 1000 + int(getBoothVoteTypeEcsa(boothName));
}

std::string get2026SeatNameFor2022SaResults(std::string const& seatName)
{
	if (seatName == "Frome") return "Ngadjuri";
	return seatName;
}

bool Results2::Election::allocate2022saDeclarationVotes(int seatId, std::string const& seatName,
  nlohmann::json& seatValue, std::map<int, int> const& indexToId)
{
  if (!seatValue.contains("booths")) return false;
  auto& seatBooths = seatValue["booths"];
  if (!seatBooths.contains("Declaration Votes")) return false;
  if (seatId <= 0) {
    logger << "WARNING: Could not resolve seat for ECSA declaration vote split in seat " << seatName << "\n";
    return false;
  }
  auto declarationBooth = seatBooths["Declaration Votes"];

  auto partyCodeForCandidateIndex = [&](int candidateIndex) {
    auto candidateIndexString = std::to_string(candidateIndex);
    if (seatValue.contains("candidates") && seatValue["candidates"].contains(candidateIndexString)) {
      auto const& candidateValue = seatValue["candidates"][candidateIndexString];
      if (candidateValue.contains("party") && candidateValue["party"].is_string()) {
        return std::string(candidateValue["party"]);
      }
    }
    if (!indexToId.contains(candidateIndex)) return std::string("IND");
    int candidateId = indexToId.at(candidateIndex);
    if (!candidates.contains(candidateId)) return std::string("IND");
    int partyId = candidates[candidateId].party;
    if (partyId < 0 || !parties.contains(partyId)) return std::string("IND");
    return parties[partyId].shortCode;
  };

  std::vector<int> orderedCandidateIndices;
  for (auto const& [fpCandIndex, _] : declarationBooth["fp"].items()) {
    orderedCandidateIndices.push_back(std::stoi(fpCandIndex));
  }
  std::sort(orderedCandidateIndices.begin(), orderedCandidateIndices.end());

  std::vector<int> candidateIds;
  std::vector<int> originalFpVotes;
  std::vector<double> originalMinorWeights;
  int laborCandidatePos = -1;
  int liberalCandidatePos = -1;
  int greensCandidatePos = -1;
  int oneNationCandidatePos = -1;
  int totalOriginalFpVotes = 0;
  for (int candidateIndex : orderedCandidateIndices) {
    int candidateId = indexToId.at(candidateIndex);
    candidateIds.push_back(candidateId);
    int votes = int(declarationBooth["fp"][std::to_string(candidateIndex)]);
    originalFpVotes.push_back(votes);
    totalOriginalFpVotes += votes;
    std::string partyCode = partyCodeForCandidateIndex(candidateIndex);
    if (partyCode == "ALP") laborCandidatePos = int(candidateIds.size()) - 1;
    else if (partyCode == "LIB") liberalCandidatePos = int(candidateIds.size()) - 1;
    else if (partyCode == "GRN") greensCandidatePos = int(candidateIds.size()) - 1;
    else if (partyCode == "ONP") oneNationCandidatePos = int(candidateIds.size()) - 1;
  }

  std::vector<int> tcpCandidateIndices;
  for (auto const& [tcpCandIndex, _] : declarationBooth["tcp"].items()) {
    tcpCandidateIndices.push_back(std::stoi(tcpCandIndex));
  }
  std::sort(tcpCandidateIndices.begin(), tcpCandidateIndices.end());
  if (tcpCandidateIndices.size() != 2) {
    logger << "Unexpected TCP structure for ECSA declaration vote in seat: " << seatName << "\n";
    return true;
  }

  std::array<int, 2> tcpCandidateIds = {
    indexToId.at(tcpCandidateIndices[0]),
    indexToId.at(tcpCandidateIndices[1])
  };
  std::array<int, 2> originalTcpVotes = {
    int(declarationBooth["tcp"][std::to_string(tcpCandidateIndices[0])]),
    int(declarationBooth["tcp"][std::to_string(tcpCandidateIndices[1])])
  };
  int totalOriginalTcpVotes = originalTcpVotes[0] + originalTcpVotes[1];

  auto originalFpShare = [&](int candidatePos) -> double {
    if (candidatePos < 0 || totalOriginalFpVotes == 0) return 0.0;
    return double(originalFpVotes[candidatePos]) / double(totalOriginalFpVotes);
  };

  double laborBaseShare = originalFpShare(laborCandidatePos);
  double liberalBaseShare = originalFpShare(liberalCandidatePos);
  double greensBaseShare = originalFpShare(greensCandidatePos);
  double oneNationBaseShare = originalFpShare(oneNationCandidatePos);

  double minorBaseShare = 1.0 - laborBaseShare - liberalBaseShare - greensBaseShare;
  if (minorBaseShare < 0.0) minorBaseShare = 0.0;
  int totalOriginalMinorVotes = 0;
  int totalOriginalNonOnpMinorVotes = 0;
  for (size_t candidatePos = 0; candidatePos < candidateIds.size(); ++candidatePos) {
    if (int(candidatePos) == laborCandidatePos ||
      int(candidatePos) == liberalCandidatePos ||
      int(candidatePos) == greensCandidatePos) {
      originalMinorWeights.push_back(0.0);
      continue;
    }
    originalMinorWeights.push_back(originalFpVotes[candidatePos]);
    totalOriginalMinorVotes += originalFpVotes[candidatePos];
    if (int(candidatePos) != oneNationCandidatePos) {
      totalOriginalNonOnpMinorVotes += originalFpVotes[candidatePos];
    }
  }

  auto buildFpVotes = [&](SaDeclarationCategory const& category, int totalCategoryVotes) {
    double laborShare = clampShare(laborBaseShare + category.laborFpOffset);
    double liberalShare = clampShare(liberalBaseShare + category.liberalFpOffset);
    double greensShare = clampShare(greensBaseShare + category.greensFpOffset);
    double majorShareSum = laborShare + liberalShare + greensShare;
    if (majorShareSum > 0.97) {
      double scale = 0.97 / majorShareSum;
      laborShare *= scale;
      liberalShare *= scale;
      greensShare *= scale;
      majorShareSum = laborShare + liberalShare + greensShare;
    }
    double remainderShare = std::max(0.0, 1.0 - majorShareSum);
    double oneNationShare = 0.0;
    if (oneNationCandidatePos >= 0) {
      oneNationShare = std::min(remainderShare, oneNationBaseShare);
      remainderShare -= oneNationShare;
    }

    std::vector<double> weights(candidateIds.size(), 0.0);
    for (size_t candidatePos = 0; candidatePos < candidateIds.size(); ++candidatePos) {
      if (int(candidatePos) == laborCandidatePos) weights[candidatePos] = laborShare;
      else if (int(candidatePos) == liberalCandidatePos) weights[candidatePos] = liberalShare;
      else if (int(candidatePos) == greensCandidatePos) weights[candidatePos] = greensShare;
      else if (int(candidatePos) == oneNationCandidatePos) weights[candidatePos] = oneNationShare;
      else if (totalOriginalNonOnpMinorVotes > 0) weights[candidatePos] = remainderShare * originalMinorWeights[candidatePos] / double(totalOriginalNonOnpMinorVotes);
      else weights[candidatePos] = remainderShare / double(std::max<size_t>(1, candidateIds.size() - size_t(laborCandidatePos >= 0) - size_t(liberalCandidatePos >= 0) - size_t(greensCandidatePos >= 0) - size_t(oneNationCandidatePos >= 0)));
    }
    return allocateVotes(totalCategoryVotes, weights, 1);
  };

  auto buildTcpVotes = [&](SaDeclarationCategory const& category, int totalCategoryVotes) {
    std::array<double, 2> weights = { 0.5, 0.5 };
    int laborTcpPos = -1;
    int liberalTcpPos = -1;
    int thirdTcpPos = -1;
    for (int pos = 0; pos < 2; ++pos) {
      std::string partyCode = partyCodeForCandidateIndex(tcpCandidateIndices[pos]);
      if (partyCode == "ALP") laborTcpPos = pos;
      else if (partyCode == "LIB") liberalTcpPos = pos;
      else thirdTcpPos = pos;
    }
    if (totalOriginalTcpVotes > 0) {
      double laborTcpShare = laborTcpPos >= 0 ? double(originalTcpVotes[laborTcpPos]) / double(totalOriginalTcpVotes) : 0.0;
      if (laborTcpPos >= 0) {
        laborTcpShare = clampShare(laborTcpShare + category.laborTcpOffset);
        if (liberalTcpPos >= 0) {
          weights[laborTcpPos] = laborTcpShare;
          weights[liberalTcpPos] = 1.0 - laborTcpShare;
        }
        else if (thirdTcpPos >= 0) {
          weights[laborTcpPos] = laborTcpShare;
          weights[thirdTcpPos] = 1.0 - laborTcpShare;
        }
      }
      else if (liberalTcpPos >= 0 && thirdTcpPos >= 0) {
        double thirdShare = double(originalTcpVotes[thirdTcpPos]) / double(totalOriginalTcpVotes);
        thirdShare = clampShare(thirdShare + category.laborTcpOffset);
        weights[thirdTcpPos] = thirdShare;
        weights[liberalTcpPos] = 1.0 - thirdShare;
      }
    }
    return allocateVotes(totalCategoryVotes, { weights[0], weights[1] }, 1);
  };

  auto makeBoothJson = [&](std::vector<int> const& fpVotes, std::array<int, 2> const& tcpVotes) {
    nlohmann::json boothJson;
    for (size_t candidatePos = 0; candidatePos < candidateIds.size(); ++candidatePos) {
      boothJson["fp"][std::to_string(orderedCandidateIndices[candidatePos])] = fpVotes[candidatePos];
    }
    for (int tcpPos = 0; tcpPos < 2; ++tcpPos) {
      boothJson["tcp"][std::to_string(tcpCandidateIndices[tcpPos])] = tcpVotes[tcpPos];
    }
    return boothJson;
  };

  auto addOrMergeBooth = [&](std::string const& boothName, std::vector<int> const& fpVotes, std::array<int, 2> const& tcpVotes) {
    nlohmann::json boothJson = makeBoothJson(fpVotes, tcpVotes);
    if (!seatBooths.contains(boothName)) {
      seatBooths[boothName] = boothJson;
      return;
    }
    for (auto const& [fpCandIndex, fpVotesValue] : boothJson["fp"].items()) {
      seatBooths[boothName]["fp"][fpCandIndex] = int(seatBooths[boothName]["fp"].value(fpCandIndex, 0)) + int(fpVotesValue);
    }
    for (auto const& [tcpCandIndex, tcpVotesValue] : boothJson["tcp"].items()) {
      seatBooths[boothName]["tcp"][tcpCandIndex] = int(seatBooths[boothName]["tcp"].value(tcpCandIndex, 0)) + int(tcpVotesValue);
    }
  };

  std::vector<int> ppvcBoothIds;
  for (auto const& [existingBoothId, existingBooth] : booths) {
    if (existingBooth.parentSeat == seatId && existingBooth.type == Booth::Type::Ppvc) {
      ppvcBoothIds.push_back(existingBoothId);
    }
  }
  std::sort(ppvcBoothIds.begin(), ppvcBoothIds.end());

  auto ppvcSplitWeights = [&]() {
    std::vector<double> weights(ppvcBoothIds.size(), 1.0);
    if (seatName != "Stuart" || ppvcBoothIds.size() != 2) return weights;

    int portPiriePos = -1;
    int portAugustaPos = -1;
    for (size_t boothPos = 0; boothPos < ppvcBoothIds.size(); ++boothPos) {
      std::string const& boothName = booths[ppvcBoothIds[boothPos]].name;
      if (boothName == "Port Pirie Early Voting Centre") portPiriePos = int(boothPos);
      else if (boothName == "Port Augusta Early Voting Centre") portAugustaPos = int(boothPos);
    }

    if (portPiriePos >= 0 && portAugustaPos >= 0) {
      weights[portPiriePos] = 0.8;
      weights[portAugustaPos] = 0.2;
    }
    return weights;
  };

  std::vector<double> baseWeights;
  for (auto const& category : SaDeclarationBaseCategories) {
    baseWeights.push_back(category.share);
  }
  auto baseFpTotals = allocateVotes(totalOriginalFpVotes, baseWeights, 0);
  auto baseTcpTotals = allocateVotes(totalOriginalTcpVotes, baseWeights, 0);

  for (size_t categoryIndex = 0; categoryIndex < SaDeclarationBaseCategories.size(); ++categoryIndex) {
    auto const& category = SaDeclarationBaseCategories[categoryIndex];
    int categoryFpTotal = baseFpTotals[categoryIndex];
    int categoryTcpTotal = baseTcpTotals[categoryIndex];
    auto categoryFpVotes = buildFpVotes(category, categoryFpTotal);
    auto categoryTcpVector = buildTcpVotes(category, categoryTcpTotal);
    std::array<int, 2> categoryTcpVotes = { categoryTcpVector[0], categoryTcpVector[1] };

    if (category.distributeAcrossPpvc) {
      if (ppvcBoothIds.empty()) {
        logger << "WARNING: No PPVC booths found for seat " << seatName << " when splitting declaration vote\n";
        addOrMergeBooth("Early Voting Absent Ordinary Votes", categoryFpVotes, categoryTcpVotes);
        continue;
      }
      auto boothWeights = ppvcSplitWeights();
      auto boothFpTotals = allocateVotes(categoryFpTotal, boothWeights, 0);
      auto boothTcpTotals = allocateVotes(categoryTcpTotal, boothWeights, 0);
      for (size_t boothPos = 0; boothPos < ppvcBoothIds.size(); ++boothPos) {
        int boothId = ppvcBoothIds[boothPos];
        auto boothFpVotes = allocateVotes(boothFpTotals[boothPos],
          std::vector<double>(categoryFpVotes.begin(), categoryFpVotes.end()), 0);
        auto boothTcpVector = allocateVotes(boothTcpTotals[boothPos],
          { double(categoryTcpVotes[0]), double(categoryTcpVotes[1]) }, 0);
        addOrMergeBooth(booths[boothId].name, boothFpVotes, { boothTcpVector[0], boothTcpVector[1] });
      }
    }
    else {
      addOrMergeBooth(category.boothName, categoryFpVotes, categoryTcpVotes);
    }
  }

  for (auto const& category : SaDeclarationExtraCategories) {
    int categoryFpTotal = std::max(1, int(std::round(totalOriginalFpVotes * category.share)));
    int categoryTcpTotal = std::max(1, int(std::round(totalOriginalTcpVotes * category.share)));
    auto categoryFpVotes = buildFpVotes(category, categoryFpTotal);
    auto categoryTcpVector = buildTcpVotes(category, categoryTcpTotal);
    addOrMergeBooth(category.boothName, categoryFpVotes, { categoryTcpVector[0], categoryTcpVector[1] });
  }

  seatBooths.erase("Declaration Votes");
  return true;
}


void Results2::Election::preloadEcsa([[maybe_unused]] nlohmann::json const& results, tinyxml2::XMLDocument const& zeros)
{
	auto const& detail = requiredChild(zeros, "HouseOfAssemblyDetail", "ECSA preload");
	auto const& districts = requiredChild(detail, "districts", "ECSA preload/HouseOfAssemblyDetail");
	auto currentDistrict = districts.FirstChildElement("district");
  if (!currentDistrict) {
    throwInvalidXml("ECSA preload/HouseOfAssemblyDetail/districts", "missing <district>");
  }
	int partyIdCounter = 1;
	std::map<std::string, int> partyNameToPartyId;
  std::map<int, std::vector<int>> seatCandidateIds;
  std::map<CandidateMatchKey, int> ecsaCandidateMatchToId;
	partyNameToPartyId["IND"] = -1;
	while (currentDistrict) {
		Seat seat;
    seat.name = requiredText(
      requiredChild(*currentDistrict, "district_name", "ECSA preload/district"),
      "ECSA preload/district/district_name");
    seat.id = requiredIntText(
      requiredChild(*currentDistrict, "district_id", "ECSA preload/district"),
      "ECSA preload/district/district_id", 0);
    if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
    auto const seatContext = "ECSA preload/district[" + seat.name + "]";
    auto const& firstPreferences = requiredChild(*currentDistrict, "first_preferences", seatContext);
    auto currentCandidate = firstPreferences.FirstChildElement("candidate");
    if (!currentCandidate) throwInvalidXml(seatContext + "/first_preferences", "missing <candidate>");
    while (currentCandidate) {
      Candidate candidate;
      candidate.name = requiredText(
        requiredChild(*currentCandidate, "candidate_name",
          seatContext + "/first_preferences/candidate"),
        seatContext + "/first_preferences/candidate/candidate_name");
      candidate.id = requiredIntText(
        requiredChild(*currentCandidate, "candidate_id",
          seatContext + "/first_preferences/candidate"),
        seatContext + "/first_preferences/candidate/candidate_id", 0);
      auto const partyName = requiredText(
        requiredChild(*currentCandidate, "affiliation",
          seatContext + "/first_preferences/candidate"),
        seatContext + "/first_preferences/candidate/affiliation");
      if (!partyNameToPartyId.contains(partyName)) {
        partyNameToPartyId[partyName] = partyIdCounter;
        ++partyIdCounter;
      }
      candidate.party = partyNameToPartyId[partyName];
      if (!parties.contains(candidate.party)) {
        parties[candidate.party] = Party();
        parties[candidate.party].id = candidate.party;
        parties[candidate.party].name = partyName;
        parties[candidate.party].shortCode = partyName;
      }
			candidates[candidate.id] = candidate;
      registerEcsaCandidateMatch(ecsaCandidateMatchToId, candidate, parties.at(candidate.party));
      seatCandidateIds[seat.id].push_back(candidate.id);
			if (candidateNameToId.contains(candidate.name)) {
				logger << "WARNING: Identical candidate names found: " << candidate.name << "\n";
			}
      candidateNameToId[candidate.name] = candidate.id;

      auto const& pollingPlaces = requiredChild(*currentCandidate, "polling_places",
        seatContext + "/first_preferences/candidate");
      auto currentBooth = pollingPlaces.FirstChildElement("polling_place");
      while (currentBooth) {
        int boothId = integerTextOrZero(
          requiredChild(*currentBooth, "polling_place_id",
            seatContext + "/first_preferences/candidate/polling_places/polling_place"),
          seatContext + "/first_preferences/candidate/polling_places/polling_place/polling_place_id");
        std::string boothName = requiredText(
          requiredChild(*currentBooth, "polling_place_name",
            seatContext + "/first_preferences/candidate/polling_places/polling_place"),
          seatContext + "/first_preferences/candidate/polling_places/polling_place/polling_place_name");
        auto voteType = getBoothVoteTypeEcsa(boothName);
        if (voteType != VoteType::Ordinary) {
          currentBooth = currentBooth->NextSiblingElement("polling_place");
          continue; // Don't actually create booths for declaration votes
        }
        if (boothId == 0) boothId = generateBoothIdEcsa(seat.id, boothName);
        if (!booths.contains(boothId)) {
          booths[boothId] = Booth();
          booths[boothId].id = boothId;
          booths[boothId].name = boothName;
          booths[boothId].parentSeat = seat.id;
          // If the booth name contains "Early Voting Centre" then mark it as a PPVC
          if (booths[boothId].name.find("Early Voting Centre") != std::string::npos
            || booths[boothId].name.find("Early voting Centre") != std::string::npos) {
            booths[boothId].type = Booth::Type::Ppvc;
          }
          if (results.is_null()) {
            // If there are no results - we're doing the current election,
            // we won't need to do a booth alignment so adding booths to seats
            // need to happen here
            seat.booths.push_back(boothId);
          }
        }
        currentBooth = currentBooth->NextSiblingElement("polling_place");
      }

      currentCandidate = currentCandidate->NextSiblingElement("candidate");
    }

    seats[seat.id] = seat;
    currentDistrict = currentDistrict->NextSiblingElement("district");
  }

  if (!results.is_null()) {
    // A lot of this is just a straight up copy of the Victorian Election
    // In summary, the purpose is to match past booth data to currents booth data
    // The length of the code is primarily to deal with various types of ambiguities
    // that can result from booths with the same name under redistributions
    // (and can therefore take on vastly different character depending on the
    // boundary changes) so these are largely removed unless the booth
    // changed cleanly from one seat to another.
    std::map<std::string, int> seatNameToId;
    for (auto [seatId, seat] : seats) {
      if (seatNameToId.contains(seat.name)) {
        logger << "Warning: duplicate name for " << seat.name << "!\n";
      }
      else {
        seatNameToId[seat.name] = seatId;
      }
    }
    std::set<std::string> seenBooths;
    std::set<std::string> ambiguousBooths;
    for (auto [boothId, booth] : booths) {
      if (seenBooths.contains(booth.name)) {
        ambiguousBooths.emplace(booth.name);
      }
      else {
        seenBooths.emplace(booth.name);
      }
    }
    std::map<std::string, int> boothNameToId;
    std::map<std::pair<std::string, std::string>, int> ambiguousBoothNameToId;
    for (auto [boothId, booth] : booths) {
      if (ambiguousBooths.contains(booth.name)) {
        ambiguousBoothNameToId[{booth.name, seats[booth.parentSeat].name}] = boothId;
      }
      else {
        boothNameToId[booth.name] = boothId;
      }
    }
    int dummyCandidateId = -100000; // Low numbers that will never be mistaken for an official id
    int dummyBoothId = -100000; // Low numbers that will never be mistaken for an official id
		std::set<int> matchedIds;
		for (auto const& [seatName, seatValue] : results.items()) {
			int seatId = -1;
      std::string mappedSeatName = get2026SeatNameFor2022SaResults(seatName);
			if (seatNameToId.contains(mappedSeatName)) {
				seatId = seatNameToId[mappedSeatName];
			}
      auto adjustedSeatValue = seatValue;
			std::map<int, int> indexToId;
			for (auto const& [candIndex, candValue] : adjustedSeatValue["candidates"].items()) {
				int candIndexI = std::stoi(candIndex);
				auto candidateName = candValue["name"];
				auto party = candValue["party"];
				if (auto matchedCandidateId = findEcsaCandidateMatch(ecsaCandidateMatchToId, candidateName, party)) {
					indexToId[candIndexI] = matchedCandidateId.value();
				}
        else if (seatId > 0 && partyNameToPartyId.contains(party)) {
          std::vector<int> matchingCurrentCandidates;
          for (int currentCandidateId : seatCandidateIds[seatId]) {
            if (candidates[currentCandidateId].party == partyNameToPartyId[party]) {
              matchingCurrentCandidates.push_back(currentCandidateId);
            }
          }
          if (matchingCurrentCandidates.size() == 1) {
            indexToId[candIndexI] = matchingCurrentCandidates.front();
          }
          else {
            indexToId[candIndexI] = dummyCandidateId;
            Candidate candidate;
            candidate.id = dummyCandidateId;
            candidate.name = candidateName;
            candidate.party = partyNameToPartyId[party];
            candidates[candidate.id] = candidate;
            registerEcsaCandidateMatch(ecsaCandidateMatchToId, candidate, parties.at(candidate.party));
            --dummyCandidateId;
          }
        }
				else {
					indexToId[candIndexI] = dummyCandidateId;
					Candidate candidate;
          candidate.id = dummyCandidateId;
          candidate.name = candidateName;
          if (partyNameToPartyId.contains(party)) {
            candidate.party = partyNameToPartyId[party];
          }
          else {
            candidate.party = -1;
          }
          candidates[candidate.id] = candidate;
          if (parties.contains(candidate.party)) {
            registerEcsaCandidateMatch(ecsaCandidateMatchToId, candidate, parties.at(candidate.party));
          }
          --dummyCandidateId;
        }
       }
      allocate2022saDeclarationVotes(seatId, seatName, adjustedSeatValue, indexToId);
      for (auto const& [boothName, boothValue] : adjustedSeatValue["booths"].items()) {
        auto voteType = getBoothVoteTypeEcsa(boothName);
        if (voteType != VoteType::Ordinary) {
          auto fps = boothValue["fp"];
          for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
            int fpCandIndexI = std::stoi(fpCandIndex);
            int fpCandId = indexToId[fpCandIndexI];
            seats[seatId].fpVotes[fpCandId][voteType] += fpVotes;
          }
          auto tcps = boothValue["tcp"];
          for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
            int tcpCandIndexI = std::stoi(tcpCandIndex);
            int tcpCandId = indexToId[tcpCandIndexI];
            int tcpAffiliation = candidates[tcpCandId].party;
            seats[seatId].tcpVotesCandidate[tcpCandId][voteType] += tcpVotes;
            seats[seatId].tcpVotes[tcpAffiliation][voteType] += tcpVotes;
          }
          continue;
        }
        int boothId = dummyBoothId;
        if (boothNameToId.contains(boothName)) {
          boothId = boothNameToId[boothName];
        }
        else if (ambiguousBoothNameToId.contains({ boothName, mappedSeatName })) {
          boothId = ambiguousBoothNameToId[{ boothName, mappedSeatName }];
        }
        else {
          Booth booth;
          booth.name = boothName;
          booth.id = dummyBoothId;
          booths[booth.id] = booth;
          --dummyBoothId;
        }
        if (matchedIds.contains(boothId)) {
          // If two "old" booths match to one "new" booth then we don't know
          // which "old" booth to actually compare to
          // so ... make new booths that won't match to either
          if (booths.contains(boothId)) {
            auto boothData = booths[boothId];
            boothData.id = dummyBoothId;
            booths.erase(boothId);
            booths[dummyBoothId] = boothData;
            auto& parent = seats[booths[dummyBoothId].parentSeat];
            auto foundBoothId = std::find(parent.booths.begin(), parent.booths.end(), boothId);
            if (foundBoothId != parent.booths.end()) *foundBoothId = dummyBoothId;
            logger << "Detached old ambiguous booth: " << booths[dummyBoothId].name << "\n";
            --dummyBoothId;
          }
          boothId = dummyBoothId;
          logger << "Detached ambiguous booth: " << boothName << "\n";
          --dummyBoothId;
        }
        matchedIds.insert(boothId);
        Booth& booth = booths[boothId];
        // the parent seat refers to the seat this booth is in for the new election,
        // but it should be the one it is in for this (old) election
        booth.parentSeat = seatId;
        booth.id = boothId;
        booth.name = boothName;
        auto fps = boothValue["fp"];
        for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
          int fpCandIndexI = std::stoi(fpCandIndex);
          int fpCandId = indexToId[fpCandIndexI];
          booth.fpVotes[fpCandId] = fpVotes;
          if (seatId > 0) seats[seatId].fpVotes[fpCandId][VoteType::Ordinary] += fpVotes;
        }
        auto tcps = boothValue["tcp"];
        for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
          int tcpCandIndexI = std::stoi(tcpCandIndex);
          int tcpCandId = indexToId[tcpCandIndexI];
          int tcpAffiliation = candidates[tcpCandId].party;
          booth.tcpVotes[tcpAffiliation] = tcpVotes;
          booth.tcpVotesCandidate[tcpCandId] = tcpVotes;
          if (seatId > 0) seats[seatId].tcpVotes[tcpAffiliation][VoteType::Ordinary] += tcpVotes;
          if (seatId > 0) seats[seatId].tcpVotesCandidate[tcpCandId][VoteType::Ordinary] += tcpVotes;
        }
        if (seatId > 0) {
          seats[seatId].booths.push_back(booth.id);
        }
      }
    }
  }

  // logger << "Ecsa Past Election Data\n";
  // PA_LOG_VAR(booths.size());
  // PA_LOG_VAR(partyNameToPartyId);
  // logger << "==SEATS==\n";
  // for (auto const& [seatId, seat] : seats) {
  //  PA_LOG_VAR(seat.name);
  //  PA_LOG_VAR(seat.id);
  //  PA_LOG_VAR(seat.fpVotes);
  //  PA_LOG_VAR(seat.tcpVotes);
  //  PA_LOG_VAR(seat.tcpVotesCandidate);
  //  PA_LOG_VAR(seat.tppVotes);
  //  PA_LOG_VAR(seat.booths);
  //  for (int boothId : seat.booths) {
  //    PA_LOG_VAR(boothId);
  //    auto const& booth = booths.at(boothId);
  //    PA_LOG_VAR(booth.id);
  //    PA_LOG_VAR(booth.name);
  //    PA_LOG_VAR(booth.fpVotes);
  //    PA_LOG_VAR(booth.tcpVotes);
  //    PA_LOG_VAR(booth.tcpVotesCandidate);
  //    PA_LOG_VAR(booth.type);
  //  }
  // }
  // logger << "==CANDIDATES==\n";
  // for (auto const& candidate : candidates) {
  //  PA_LOG_VAR(candidate.second.id);
  //  PA_LOG_VAR(candidate.second.name);
  //  if (candidate.second.party != -1) {
  //    PA_LOG_VAR(parties[candidate.second.party].name);
  //  }
  //  else {
  //    logger << "Independent\n";
  //  }
  // }
  // logger << "==PARTIES==\n";
  // for (auto const& party : parties) {
  //  PA_LOG_VAR(party.second.id);
  //  PA_LOG_VAR(party.second.name);
  //  PA_LOG_VAR(party.second.shortCode);
  // }
  //logger << "Appeared to successfully load past election data\n";
}

void Results2::Election::update(tinyxml2::XMLDocument const& xml, Format format)
{
  if (format == Format::QEC) {
    // QEC format is different enough to just have a separate procedure
    updateQec(xml);
    return;
  }
  else if (format == Format::WAEC) {
    updateWaec(xml);
    return;
  } 
  else if (format == Format::ECSA) {
    updateEcsa(xml);
    return;
  }
  std::string const feedContext = format == Format::NSWEC ? "NSWEC results" :
    format == Format::VEC ? "VEC results" : "AEC results";
  auto const* documentRoot = xml.FirstChildElement();
  if (!documentRoot) throwInvalidXml(feedContext, "document contains no root element");
  PA_LOG_VAR(documentRoot->Name());
  auto const& mediaFeed = requiredChild(xml, "MediaFeed", feedContext);
  auto resultsFinder = [&]() -> tinyxml2::XMLElement const* {
    switch (format) {
    case Format::AEC: return &requiredChild(mediaFeed, "Results", feedContext + "/MediaFeed");
    case Format::VEC: return &mediaFeed;
    case Format::NSWEC: return &mediaFeed;
    default: return &requiredChild(mediaFeed, "Results", feedContext + "/MediaFeed");
    }
  };
  auto const* results = resultsFinder();
  auto getElectionInfo = [&]() {
    switch (format) {
    case Format::NSWEC:
      {
        auto const& electionEl = requiredChild(*results, "EventIdentifier", feedContext);
        id = requiredIntegerString(
          requiredAttribute(electionEl, "Id", feedContext + "/EventIdentifier"),
          feedContext + "/EventIdentifier/@Id", 2, 0);
        name = requiredText(
          requiredChild(electionEl, "EventName", feedContext + "/EventIdentifier"),
          feedContext + "/EventIdentifier/EventName");
        break;
      }
    default:
      {
        auto const& electionEl = requiredChild(*results, "eml:EventIdentifier", feedContext);
        id = requiredIntAttribute(electionEl, "Id", feedContext + "/eml:EventIdentifier", 0);
        name = requiredText(
          requiredChild(electionEl, "eml:EventName", feedContext + "/eml:EventIdentifier"),
          feedContext + "/eml:EventIdentifier/eml:EventName");
      }
    }
  };
  getElectionInfo();
  auto const& election = requiredChild(*results, "Election", feedContext);
  auto const& house = requiredChild(election, "House", feedContext + "/Election");
  auto const& contests = requiredChild(house, "Contests", feedContext + "/Election/House");
  auto currentContest = contests.FirstChildElement("Contest");
  if (!currentContest) throwInvalidXml(feedContext + "/Election/House/Contests", "missing <Contest>");

  // For elections (like NSW) where seats and candidates aren't given their own id numbers
  int seatIdCounter = 1;
  std::string candidateIdElName = format == Format::NSWEC ? "CandidateIdentifier" : "eml:CandidateIdentifier";

  std::set<int> boothIdsPresent;

  while (currentContest) {
    Seat seat;
    auto contestIdFinder = [&]() -> tinyxml2::XMLElement const* {
      switch (format) {
      case Format::AEC: return &requiredChild(*currentContest, "eml:ContestIdentifier", feedContext + "/Contest");
      case Format::VEC: return &requiredChild(*currentContest, "PollingDistrictIdentifier", feedContext + "/Contest");
      case Format::NSWEC: return &requiredChild(*currentContest, "PollingDistrictIdentifier", feedContext + "/Contest");
      default: return &requiredChild(*currentContest, "eml:ContestIdentifier", feedContext + "/Contest");
      }
    };

    auto contestIdEl = contestIdFinder();
    if (format == Format::NSWEC) {
      seat.id = seatIdCounter;
      ++seatIdCounter;
    }
    else {
      seat.id = requiredIntAttribute(*contestIdEl, "Id", feedContext + "/Contest/ContestIdentifier", 0);
    }
    if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data

    auto nameFinder = [&]() -> std::string {
      switch (format) {
      case Format::AEC: return requiredText(
        requiredChild(*contestIdEl, "eml:ContestName", feedContext + "/Contest/ContestIdentifier"),
        feedContext + "/Contest/ContestIdentifier/eml:ContestName");
      case Format::VEC: {
        std::string prelimText = requiredText(
          requiredChild(*contestIdEl, "Name", feedContext + "/Contest/PollingDistrictIdentifier"),
          feedContext + "/Contest/PollingDistrictIdentifier/Name");
        if (prelimText.length() < 9) {
          throwInvalidXml(feedContext + "/Contest/PollingDistrictIdentifier/Name",
            "name is too short to contain the expected suffix");
        }
        return prelimText.substr(0, prelimText.length() - 9);
      }
      case Format::NSWEC: return requiredAttribute(*contestIdEl, "Id",
        feedContext + "/Contest/PollingDistrictIdentifier");
      default: return requiredText(
        requiredChild(*contestIdEl, "eml:ContestName", feedContext + "/Contest/ContestIdentifier"),
        feedContext + "/Contest/ContestIdentifier/eml:ContestName");
      }
    };

    seat.name = nameFinder();
    auto const seatContext = feedContext + "/Contest[" + seat.name + "]";
    seat.enrolment = requiredIntText(
      requiredChild(*currentContest, "Enrolment", seatContext),
      seatContext + "/Enrolment", 0);

    auto const& firstPrefs = requiredChild(*currentContest, "FirstPreferences", seatContext);
    auto currentCandidate = firstPrefs.FirstChildElement("Candidate");
    while (currentCandidate) {
      Candidate candidate;
      auto const& candidateIdEl = requiredChild(*currentCandidate,
        candidateIdElName.c_str(), seatContext + "/FirstPreferences/Candidate");

      if (format == Format::NSWEC) {
        std::string const candidateName = requiredText(
          requiredChild(candidateIdEl, "CandidateName",
            seatContext + "/FirstPreferences/Candidate/CandidateIdentifier"),
          seatContext + "/FirstPreferences/Candidate/CandidateIdentifier/CandidateName");
        auto const candidateKey = nswecCandidateKey(seat.id, candidateName);
        auto const foundCandidate = candidateNameToId.find(candidateKey);
        if (foundCandidate == candidateNameToId.end()) {
          throwInvalidXml(seatContext + "/FirstPreferences/Candidate/CandidateIdentifier",
            "candidate '" + candidateName + "' was not present in the preload");
        }
        candidate.id = foundCandidate->second;
      } else {
        candidate.id = requiredIntAttribute(candidateIdEl, "Id",
          seatContext + "/FirstPreferences/Candidate/" + candidateIdElName, 0);
      }

      // Any candidate/party data should already be preloaded for NSWEC
      if (format != Format::NSWEC && candidateIdEl.FirstChildElement("eml:CandidateName")) {
        candidate.name = requiredText(
          requiredChild(candidateIdEl, "eml:CandidateName",
            seatContext + "/FirstPreferences/Candidate/" + candidateIdElName),
          seatContext + "/FirstPreferences/Candidate/" + candidateIdElName + "/eml:CandidateName");
        auto affiliationEl = currentCandidate->FirstChildElement("eml:AffiliationIdentifier");
        if (affiliationEl) {
          candidate.party = requiredIntAttribute(*affiliationEl, "Id",
            seatContext + "/FirstPreferences/Candidate/eml:AffiliationIdentifier", 0);
          if (!parties.contains(candidate.party)) {
            parties[candidate.party] = Party();
            parties[candidate.party].id = candidate.party;
            parties[candidate.party].name = requiredText(
              requiredChild(*affiliationEl, "eml:RegisteredName",
                seatContext + "/FirstPreferences/Candidate/eml:AffiliationIdentifier"),
              seatContext + "/FirstPreferences/Candidate/eml:AffiliationIdentifier/eml:RegisteredName");
            parties[candidate.party].shortCode = requiredAttribute(*affiliationEl, "ShortCode",
              seatContext + "/FirstPreferences/Candidate/eml:AffiliationIdentifier");
          }
        }
        else candidate.party = Candidate::Independent;
        parties[Candidate::Independent].id = Candidate::Independent;
        parties[Candidate::Independent].shortCode = "IND";
        parties[Candidate::Independent].name = "Independent";
        candidates[candidate.id] = candidate;
      }
      auto const& votesByType = requiredChild(*currentCandidate, "VotesByType",
        seatContext + "/FirstPreferences/Candidate");
      auto fpVoteType = votesByType.FirstChildElement("Votes");
      while (fpVoteType) {
        std::string typeName = requiredAttribute(*fpVoteType, "Type",
          seatContext + "/FirstPreferences/Candidate/VotesByType/Votes");
        int fpCount = integerTextOrZero(*fpVoteType,
          seatContext + "/FirstPreferences/Candidate/VotesByType/Votes");
        if (typeNameToVoteType.contains(typeName)) seat.fpVotes[candidate.id][typeNameToVoteType[typeName]] += fpCount;
        fpVoteType = fpVoteType->NextSiblingElement("Votes");
      }

      currentCandidate = currentCandidate->NextSiblingElement("Candidate");
    }

    auto candidateIdFinder = [&]() {
      switch (format) {
      case Format::NSWEC:
      {
        auto const& candidateIdEl = requiredChild(*currentCandidate,
          "CandidateIdentifier", seatContext + "/Candidate");
        auto const candidateName = requiredAttribute(candidateIdEl, "Id",
          seatContext + "/Candidate/CandidateIdentifier");
        auto const candidateKey = nswecCandidateKey(seat.id, candidateName);
        auto const foundCandidate = candidateNameToId.find(candidateKey);
        if (foundCandidate == candidateNameToId.end()) {
          throwInvalidXml(seatContext + "/Candidate/CandidateIdentifier",
            "candidate '" + candidateName + "' was not present in the preload");
        }
        return foundCandidate->second;
      }
      default:
      {
        auto const& candidateIdEl = requiredChild(*currentCandidate,
          "eml:CandidateIdentifier", seatContext + "/Candidate");
        return requiredIntAttribute(candidateIdEl, "Id",
          seatContext + "/Candidate/eml:CandidateIdentifier", 0);
      }
      }
    };

    auto tcps = currentContest->FirstChildElement("TwoCandidatePreferred");
    if (tcps) {
      currentCandidate = tcps->FirstChildElement("Candidate");
      while (currentCandidate) {
        
        auto candidateId = candidateIdFinder();
        int partyId = candidates.at(candidateId).party;
        auto const& votesByType = requiredChild(*currentCandidate, "VotesByType",
          seatContext + "/TwoCandidatePreferred/Candidate");
        auto tcpVoteType = votesByType.FirstChildElement("Votes");
        while (tcpVoteType) {
          std::string typeName = requiredAttribute(*tcpVoteType, "Type",
            seatContext + "/TwoCandidatePreferred/Candidate/VotesByType/Votes");
          int tcpCount = integerTextOrZero(*tcpVoteType,
            seatContext + "/TwoCandidatePreferred/Candidate/VotesByType/Votes");
          // Note: for NSWEC the PP/PR categories are always zero - will need to extract them from the booth data
          if (typeNameToVoteType.contains(typeName)) seat.tcpVotes[partyId][typeNameToVoteType[typeName]] = tcpCount;
          if (typeNameToVoteType.contains(typeName)) seat.tcpVotesCandidate[candidateId][typeNameToVoteType[typeName]] = tcpCount;

          tcpVoteType = tcpVoteType->NextSiblingElement("Votes");
        }

        currentCandidate = currentCandidate->NextSiblingElement("Candidate");
      }
    }

    auto const& boothsEl = requiredChild(*currentContest, "PollingPlaces", seatContext);
    auto currentBooth = boothsEl.FirstChildElement("PollingPlace");
    while (currentBooth) {
      auto const& boothIdEl = requiredChild(*currentBooth,
        "PollingPlaceIdentifier", seatContext + "/PollingPlaces/PollingPlace");
      Booth booth;
      auto const boothName = boothIdEl.FindAttribute("Name") ?
        requiredAttribute(boothIdEl, "Name",
          seatContext + "/PollingPlaces/PollingPlace/PollingPlaceIdentifier") : "";
      if (boothName == "Absent" || boothName == "Enrolment/Provisional" ||
        boothName == "Postal" || boothName == "iVote") {
        currentBooth = currentBooth->NextSiblingElement("PollingPlace");
        continue;
      }
      booth.id = requiredIntAttribute(boothIdEl, "Id",
        seatContext + "/PollingPlaces/PollingPlace/PollingPlaceIdentifier", 0);
      if (format == Format::NSWEC) booth.id += seat.id * 100000; // create unique booth ID for booths with the same name in different seats
      boothIdsPresent.emplace(booth.id);
      if (booths.contains(booth.id)) booth = booths[booth.id]; // maintain already existing data
      if (boothIdEl.FindAttribute("Name")) {
        booth.parentSeat = seat.id;
        booth.name = boothName;
        if (booth.name == "Declared Facility" || booth.name == "Sydney Town Hall") {
          booth.name += " (" + seat.name + ")";
        }

        auto classifierEl = boothIdEl.FindAttribute("Classification");
        if (classifierEl) {
          std::string classifier = classifierEl->Value();
          if (classifier == "PrePollVotingCentre") booth.type = Booth::Type::Ppvc;
          if (classifier == "PrisonMobile") booth.type = Booth::Type::Prison;
          if (classifier == "SpecialHospital") booth.type = Booth::Type::Hospital;
          if (classifier == "RemoteMobile") booth.type = Booth::Type::Remote;
          // These are technically PPVCs but they're very volatile and shouldn't be used as a proxy for normal PPVCs.
          if (booth.name.find("Divisional Office") != std::string::npos) booth.type = Booth::Type::Other;
          if (booth.name.find("BLV") != std::string::npos) booth.type = Booth::Type::Other;
        }

        // Don't add the same booth multiple times
        if (std::find(seat.booths.begin(), seat.booths.end(), booth.id) == seat.booths.end()) {
          seat.booths.push_back(booth.id);
        }
      }

      auto const& fps = requiredChild(*currentBooth, "FirstPreferences",
        seatContext + "/PollingPlaces/PollingPlace");
      currentCandidate = fps.FirstChildElement("Candidate");
      while (currentCandidate) {
        int candidateId = candidateIdFinder();
        int votes = integerTextOrZero(
          requiredChild(*currentCandidate, "Votes",
            seatContext + "/PollingPlaces/PollingPlace/FirstPreferences/Candidate"),
          seatContext + "/PollingPlaces/PollingPlace/FirstPreferences/Candidate/Votes");
        booth.fpVotes[candidateId] = votes;

        currentCandidate = currentCandidate->NextSiblingElement("Candidate");
      }

      tcps = currentBooth->FirstChildElement("TwoCandidatePreferred");
      if (tcps) {
        currentCandidate = tcps->FirstChildElement("Candidate");
        while (currentCandidate) {
          int candidateId = candidateIdFinder();
          int partyId = candidates.at(candidateId).party;
          int votes = integerTextOrZero(
            requiredChild(*currentCandidate, "Votes",
              seatContext + "/PollingPlaces/PollingPlace/TwoCandidatePreferred/Candidate"),
            seatContext + "/PollingPlaces/PollingPlace/TwoCandidatePreferred/Candidate/Votes");
          booth.tcpVotes[partyId] = votes;
          booth.tcpVotesCandidate[candidateId] = votes;

          currentCandidate = currentCandidate->NextSiblingElement("Candidate");
        }
      }

      booths[booth.id] = booth;

      currentBooth = currentBooth->NextSiblingElement("PollingPlace");
    }

    seats[seat.id] = seat;

    currentContest = currentContest->NextSiblingElement("Contest");
  }

  for (auto& [seatId, seat] : seats) {
    std::vector<int> boothsToErase;
    for (int boothId : seat.booths) {
      if (!boothIdsPresent.contains(boothId)) {
        auto const& booth = booths.at(boothId);
        logger << "Removing booth " << booth.name << " (" << booth.id << ") from " << seat.name << " as it is not present.\n";
        boothsToErase.push_back(boothId);
      }
    }
    for (int boothToErase : boothsToErase) {
      seat.booths.erase(std::remove(seat.booths.begin(), seat.booths.end(), boothToErase), seat.booths.end());
    }
  }

  applyResultOverrides();

  // logger << "==SEATS==\n";
  //   for (auto const& [seatId, seat] : seats) {
  // 	PA_LOG_VAR(seat.name);
  // 	PA_LOG_VAR(seat.id);
  // 	PA_LOG_VAR(seat.enrolment);
  // 	PA_LOG_VAR(seat.fpVotes);
  // 	PA_LOG_VAR(seat.tcpVotes);
  // 	PA_LOG_VAR(seat.tppVotes);
  // 	PA_LOG_VAR(seat.booths);
  //  	for (int boothId : seat.booths) {
  //  		auto const& booth = booths.at(boothId);
  //  		PA_LOG_VAR(booth.id);
  //  		PA_LOG_VAR(booth.name);
  // 		PA_LOG_VAR(booth.fpVotes);
  // 		PA_LOG_VAR(booth.tcpVotes);
  // 		PA_LOG_VAR(booth.tcpVotesCandidate);
  // 		PA_LOG_VAR(booth.tppVotes);
  // 		PA_LOG_VAR(booth.type);
  // 	}
  // }
  // logger << "==CANDIDATES==\n";
  // for (auto const& candidate : candidates) {
  // 	PA_LOG_VAR(candidate.second.id);
  // 	PA_LOG_VAR(candidate.second.name);
  // 	if (candidate.second.party != -1) {
  // 		PA_LOG_VAR(parties[candidate.second.party].name);
  // 	}
  // 	else {
  // 		logger << "Independent\n";
  // 	}
  // }
  // logger << "==PARTIES==\n";
  // for (auto const& party : parties) {
  // 	PA_LOG_VAR(party.second.id);
  // 	PA_LOG_VAR(party.second.name);
  // 	PA_LOG_VAR(party.second.shortCode);
  // }
  // logger << "==COALITIONS==\n";
  // for (auto const& coalition : coalitions) {
  // 	PA_LOG_VAR(coalition.second.id);
  // 	PA_LOG_VAR(coalition.second.name);
  // 	PA_LOG_VAR(coalition.second.shortCode);
  // }
  // logger << "==ELECTION==\n";
  // PA_LOG_VAR(name);
  // PA_LOG_VAR(id);
}

void Results2::Election::updateQec(tinyxml2::XMLDocument const& xml)
{
  // This is a pure update, assumes you've already used the preload
  const std::map<std::string, std::string> shortCodes = {
    {"Queensland Greens", "GRN"},
    {"Australian Labor Party (State of Queensland)", "ALP"},
    {"The Liberal Party of Australia, New South Wales Division", "LNP"},
    {"Pauline Hanson's One Nation Queensland Division", "ON"},
    {"Katter's Australian Party (KAP)", "KAP"},
    {"Family First Queensland", "FF"},
    {"Independent", "IND"}
  };

  auto const& ecq = requiredChild(xml, "ecq", "QEC results");
  auto const& election = requiredChild(ecq, "election", "QEC results/ecq");
  id = requiredIntAttribute(election, "id", "QEC results/ecq/election", 0);
  auto const* electionName = election.Attribute("electionName");
  name = electionName ? electionName : "";
  auto const& districts = requiredChild(election, "districts", "QEC results/ecq/election");
  auto currentDistrict = districts.FirstChildElement("district");
  if (!currentDistrict) throwInvalidXml("QEC results/ecq/election/districts", "missing <district>");
  std::map<std::string, int> partyNameToPartyId;
  while (currentDistrict) {
    Seat seat;
    seat.id = requiredIntAttribute(*currentDistrict, "number",
      "QEC results/ecq/election/districts/district", 0);
    if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
    auto const seatContext = "QEC results/district[" + std::to_string(seat.id) + "]";
    seat.enrolment = requiredIntAttribute(*currentDistrict, "enrolment", seatContext, 0);

    // Locate rounds by name: an early feed may omit the indicative count, and
    // relying on sibling order makes that absence look like malformed XML.
    tinyxml2::XMLElement const* fps = nullptr;
    tinyxml2::XMLElement const* tcps = nullptr;
    for (auto const* countRound = currentDistrict->FirstChildElement("countRound");
      countRound; countRound = countRound->NextSiblingElement("countRound")) {
      auto const* countName = countRound->Attribute("countName");
      if (!countName) continue;
      if (std::string_view(countName) == "Unofficial Preliminary Count") fps = countRound;
      else if (std::string_view(countName) == "Unofficial Indicative Count") tcps = countRound;
    }

    auto getBoothVoteType = [](std::string boothName) {
      if (boothName == "Mobile Polling") return VoteType::PrePoll;
      if (boothName == "Telephone Voting") return VoteType::Telephone;
      if (boothName == "Telephone Voting - Early Voting") return VoteType::Telephone;
      if (boothName == "Postal Declaration Votes") return VoteType::Postal;
      if (boothName == "In Person Declaration Votes") return VoteType::Provisional;
      if (boothName == "Absent Election Day") return VoteType::Absent;
      if (boothName == "Absent Early Voting") return VoteType::PrePoll;
      return VoteType::Ordinary;
    };

    // Extract booth fp results
    auto const* fpBooths = fps ? fps->FirstChildElement("booths") : nullptr;
    if (fpBooths) {
      auto currentFpBooth = fpBooths->FirstChildElement("booth");
      for (; currentFpBooth; currentFpBooth = currentFpBooth->NextSiblingElement("booth")) {
        if (!currentFpBooth->FirstChildElement("primaryVoteResults")) continue;
        auto boothId = requiredIntAttribute(*currentFpBooth, "id",
          seatContext + "/preliminary/booths/booth", 0) + seat.id * 100000;
        if (!booths.contains(boothId)) continue; // ignore booths not in preload
        Booth& booth = booths[boothId];
        auto currentCandidate = currentFpBooth->FirstChildElement("primaryVoteResults")->FirstChildElement("candidate");
        for (; currentCandidate; currentCandidate = currentCandidate->NextSiblingElement("candidate")) {
          auto candidateName = requiredAttribute(*currentCandidate, "ballotName",
            seatContext + "/preliminary/booths/booth/primaryVoteResults/candidate");
          auto const candidateKey = qecCandidateKey(seat.id, candidateName);
          if (!candidateNameToId.contains(candidateKey)) continue;
          auto candidateId = candidateNameToId[candidateKey];
          auto votes = requiredIntText(
            requiredChild(*currentCandidate, "count",
              seatContext + "/preliminary/booths/booth/primaryVoteResults/candidate"),
            seatContext + "/preliminary/booths/booth/primaryVoteResults/candidate/count", 0);
          booth.fpVotes[candidateId] = votes;
          seat.fpVotes[candidateId][getBoothVoteType(booth.name)] += votes;
        }
      }
    }

    // Extract booth tcp results
    auto const* tcpBooths = tcps ? tcps->FirstChildElement("booths") : nullptr;
    if (tcpBooths) {
      auto currentTcpBooth = tcpBooths->FirstChildElement("booth");
      for (; currentTcpBooth; currentTcpBooth = currentTcpBooth->NextSiblingElement("booth")) {
        if (!currentTcpBooth->FirstChildElement("twoCandidateVotes")) continue;
        auto boothId = requiredIntAttribute(*currentTcpBooth, "id",
          seatContext + "/indicative/booths/booth", 0) + seat.id * 100000;
        if (!booths.contains(boothId)) continue; // ignore booths not in preload
        Booth& booth = booths[boothId];
        auto currentCandidate = currentTcpBooth->FirstChildElement("twoCandidateVotes")->FirstChildElement("candidate");
        for (; currentCandidate; currentCandidate = currentCandidate->NextSiblingElement("candidate")) {
          auto candidateName = requiredAttribute(*currentCandidate, "ballotName",
            seatContext + "/indicative/booths/booth/twoCandidateVotes/candidate");
          auto const candidateKey = qecCandidateKey(seat.id, candidateName);
          if (!candidateNameToId.contains(candidateKey)) continue;
          auto candidateId = candidateNameToId[candidateKey];
          int partyId = candidates.at(candidateId).party;
          auto votes = requiredIntText(
            requiredChild(*currentCandidate, "count",
              seatContext + "/indicative/booths/booth/twoCandidateVotes/candidate"),
            seatContext + "/indicative/booths/booth/twoCandidateVotes/candidate/count", 0);
          booth.tcpVotes[partyId] = votes;
          seat.tcpVotes[partyId][getBoothVoteType(booth.name)] += votes;
        }
      }
    }

    seats[seat.id] = seat;
    currentDistrict = currentDistrict->NextSiblingElement("district");
  }

  // logger << "Qec Update\n";
  // PA_LOG_VAR(booths.size());
  // logger << "==SEATS==\n";
  // for (auto const& [seatId, seat] : seats) {
  // 	PA_LOG_VAR(seat.name);
  // 	PA_LOG_VAR(seat.id);
  // 	PA_LOG_VAR(seat.enrolment);
  // 	PA_LOG_VAR(seat.fpVotes);
  // 	PA_LOG_VAR(seat.tcpVotes);
  // 	PA_LOG_VAR(seat.tppVotes);
  // 	PA_LOG_VAR(seat.booths);
  // 	for (int boothId : seat.booths) {
  // 		PA_LOG_VAR(boothId);
  // 		auto const& booth = booths.at(boothId);
  // 		PA_LOG_VAR(booth.id);
  // 		PA_LOG_VAR(booth.name);
  // 		PA_LOG_VAR(booth.fpVotes);
  // 		PA_LOG_VAR(booth.tcpVotes);
  // 		PA_LOG_VAR(booth.type);
  // 	}
  // }
  // logger << "==CANDIDATES==\n";
  // for (auto const& candidate : candidates) {
  // 	PA_LOG_VAR(candidate.second.id);
  // 	PA_LOG_VAR(candidate.second.name);
  // 	if (candidate.second.party != -1) {
  // 		PA_LOG_VAR(parties[candidate.second.party].name);
  // 	}
  // 	else {
  // 		logger << "Independent\n";
  // 	}
  // }
  // logger << "==PARTIES==\n";
  // for (auto const& party : parties) {
  // 	PA_LOG_VAR(party.second.id);
  // 	PA_LOG_VAR(party.second.name);
  // 	PA_LOG_VAR(party.second.shortCode);
  // }
  logger << "Appeared to successfully load election data update\n";
}

void Results2::Election::updateWaec(tinyxml2::XMLDocument const& xml)
{
  // This is a pure update, assumes you've already used the preload

  auto VoteTypeCategory = [](std::string boothName) {
    if (boothName == "Special Institutions,  Hospitals & Remotes") return VoteType::SIR;
    if (boothName == "Absent Votes") return VoteType::Absent;
    if (boothName == "Early Votes (by Post)") return VoteType::Postal;
    if (boothName == "Early Votes (In Person)") return VoteType::PrePoll;
    if (boothName == "Provisional Votes") return VoteType::Provisional;
    return VoteType::Invalid;
  };

  auto currentRegion = xml.FirstChildElement()->FirstChildElement("ElectionRegion");
  while (currentRegion) {
    auto currentDistrict = currentRegion->FirstChildElement("ElectionDistrict");
    while (currentDistrict) {
      int seatId = hashName(currentDistrict->Attribute("Name"));
      Seat& seat = seats.at(seatId);

      auto districtVotes = currentDistrict->FirstChildElement("LA")->FirstChildElement("DistrictVotes");

      // We need to know all the candidate ids for this seat to establish zeros for other vote types
      std::vector<int> candidateIds;
      auto currentCandidate = districtVotes->FirstChildElement("CandidateVotes");
      while (currentCandidate) {
        auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
        if (!candidateNameToId.contains(candidateName)) {
          currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
          continue;
        }
        candidateIds.push_back(candidateNameToId[candidateName]);
        currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
      }

      auto currentBooth = districtVotes->FirstChildElement("OrdinaryPollingPlaceVotes");
      while (currentBooth) {

        int boothId = currentBooth->IntAttribute("VenueId");
        if (booths.contains(boothId)) {
          Booth& booth = booths[boothId];
          currentCandidate = currentBooth->FirstChildElement("CandidateVotes");
          while (currentCandidate) {
            auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
            if (!candidateNameToId.contains(candidateName)) {
              currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
              continue;
            }
            auto candidateId = candidateNameToId[candidateName];
            auto votes = currentCandidate->IntAttribute("Votes");
            booth.fpVotes[candidateId] = votes;
            currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
          }
        }

        currentBooth = currentBooth->NextSiblingElement("OrdinaryPollingPlaceVotes");
      }

      auto currentCategory = districtVotes->FirstChildElement("CategoryVotes");
      while (currentCategory) {
        auto categoryName = currentCategory->Attribute("CategoryName");
        auto categoryType = VoteTypeCategory(categoryName);
        if (categoryType == VoteType::Invalid) {
          currentCategory = currentCategory->NextSiblingElement("CategoryVotes");
          continue;
        }
        currentCandidate = currentCategory->FirstChildElement("CandidateVotes");
        while (currentCandidate) {
          auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
          if (!candidateNameToId.contains(candidateName)) {
            currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
            continue;
          }
          auto candidateId = candidateNameToId[candidateName];
          auto votes = currentCandidate->IntAttribute("Votes");
          seat.fpVotes[candidateId][categoryType] = votes;
          currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
        }
        currentCategory = currentCategory->NextSiblingElement("CategoryVotes");
      }
      districtVotes = districtVotes->NextSiblingElement("DistrictVotes");

      if (districtVotes) {

        currentBooth = districtVotes->FirstChildElement("OrdinaryPollingPlaceVotes");
        while (currentBooth) {

          int boothId = currentBooth->IntAttribute("VenueId");
          if (booths.contains(boothId)) {
            Booth& booth = booths[boothId];
            currentCandidate = currentBooth->FirstChildElement("CandidateVotes");
            while (currentCandidate) {
              auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
              if (!candidateNameToId.contains(candidateName)) {
                currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
                continue;
              }
              auto candidateId = candidateNameToId[candidateName];
              auto votes = currentCandidate->IntAttribute("Votes");
              booth.tcpVotesCandidate[candidateId] = votes;
              auto partyId = candidates.at(candidateId).party;
              booth.tcpVotes[partyId] = votes;
              currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
            }
          }
          currentBooth = currentBooth->NextSiblingElement("OrdinaryPollingPlaceVotes");
        }

        currentCategory = districtVotes->FirstChildElement("CategoryVotes");
        while (currentCategory) {
          auto categoryName = currentCategory->Attribute("CategoryName");
          auto categoryType = VoteTypeCategory(categoryName);
          if (categoryType == VoteType::Invalid) {
            currentCategory = currentCategory->NextSiblingElement("CategoryVotes");
            continue;
          }
          currentCandidate = currentCategory->FirstChildElement("CandidateVotes");
          while (currentCandidate) {
            auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
            if (!candidateNameToId.contains(candidateName)) {
              currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
              continue;
            }
            auto candidateId = candidateNameToId[candidateName];
            auto votes = currentCandidate->IntAttribute("Votes");
            seat.tcpVotesCandidate[candidateId][categoryType] = votes;
            auto partyId = candidates.at(candidateId).party;
            seat.tcpVotes[partyId][categoryType] = votes;
            currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
          }
          currentCategory = currentCategory->NextSiblingElement("CategoryVotes");
        }
      }

      std::vector expectedVoteTypes = {
        VoteType::SIR, 
        VoteType::Absent,
        VoteType::Postal,
        VoteType::PrePoll,
        VoteType::Provisional
      };
      for (auto voteType : expectedVoteTypes) {
        for (auto candidateId : candidateIds) {
          if (seat.fpVotes.find(candidateId) == seat.fpVotes.end()) {
            seat.fpVotes[candidateId] = {};
          }
          if (seat.tcpVotes.find(candidateId) == seat.tcpVotes.end()) {
            seat.tcpVotes[candidateId] = {};
          }
          if (seat.tcpVotesCandidate.find(candidateId) == seat.tcpVotesCandidate.end()) {
            seat.tcpVotesCandidate[candidateId] = {};
          }
          if (seat.fpVotes[candidateId].find(voteType) == seat.fpVotes[candidateId].end()) {
            seat.fpVotes[candidateId][voteType] = 0;
          }
          if (seat.tcpVotes[candidateId].find(voteType) == seat.tcpVotes[candidateId].end()) {
            seat.tcpVotes[candidateId][voteType] = 0;
          }
          if (seat.tcpVotesCandidate[candidateId].find(voteType) == seat.tcpVotesCandidate[candidateId].end()) {
            seat.tcpVotesCandidate[candidateId][voteType] = 0;
          }
        }
      }
      currentDistrict = currentDistrict->NextSiblingElement("ElectionDistrict");
    }

    currentRegion = currentRegion->NextSiblingElement("ElectionRegion");
  }

  // logger << "Waec Update\n";
  // PA_LOG_VAR(booths.size());
  // logger << "==SEATS==\n";
  // for (auto const& [seatId, seat] : seats) {
  // 	PA_LOG_VAR(seat.name);
  // 	PA_LOG_VAR(seat.id);
  // 	PA_LOG_VAR(seat.enrolment);
  // 	PA_LOG_VAR(seat.fpVotes);
  // 	PA_LOG_VAR(seat.tcpVotes);
  // 	PA_LOG_VAR(seat.tcpVotesCandidate);
  // 	PA_LOG_VAR(seat.tppVotes);
  // 	PA_LOG_VAR(seat.booths);
  // 	for (int boothId : seat.booths) {
  // 		PA_LOG_VAR(boothId);
  // 		auto const& booth = booths.at(boothId);
  // 		PA_LOG_VAR(booth.id);
  // 		PA_LOG_VAR(booth.name);
  // 		PA_LOG_VAR(booth.fpVotes);
  // 		PA_LOG_VAR(booth.tcpVotes);
  // 		PA_LOG_VAR(booth.type);
  // 	}
  // }
  // logger << "==CANDIDATES==\n";
  // for (auto const& candidate : candidates) {
  // 	PA_LOG_VAR(candidate.second.id);
  // 	PA_LOG_VAR(candidate.second.name);
  // 	if (candidate.second.party != -1) {
  // 		PA_LOG_VAR(parties[candidate.second.party].name);
  // 	}
  // 	else {
  // 		logger << "Independent\n";
  // 	}
  // }
  // logger << "==PARTIES==\n";
  // for (auto const& party : parties) {
  // 	PA_LOG_VAR(party.second.id);
  // 	PA_LOG_VAR(party.second.name);
  // 	PA_LOG_VAR(party.second.shortCode);
  // }
  logger << "Appeared to successfully load election data update\n";
}

void Results2::Election::updateEcsa(tinyxml2::XMLDocument const& xml)
{
  // This is a pure update, assumes the preload has already been used

  auto const& detail = requiredChild(xml, "HouseOfAssemblyDetail", "ECSA results");
  auto const& districts = requiredChild(detail, "districts", "ECSA results/HouseOfAssemblyDetail");
  auto currentDistrict = districts.FirstChildElement("district");
  if (!currentDistrict) {
    throwInvalidXml("ECSA results/HouseOfAssemblyDetail/districts", "missing <district>");
  }
  while (currentDistrict) {
    int seatId = requiredIntText(
      requiredChild(*currentDistrict, "district_id", "ECSA results/district"),
      "ECSA results/district/district_id", 0);
    if (!seats.contains(seatId)) {
      currentDistrict = currentDistrict->NextSiblingElement("district");
      continue;
    }
    Seat& seat = seats[seatId]; // maintain already existing data
    auto const seatContext = "ECSA results/district[" + seat.name + "]";

    auto const& firstPreferences = requiredChild(*currentDistrict, "first_preferences", seatContext);
    auto currentCandidate = firstPreferences.FirstChildElement("candidate");
    while (currentCandidate) {
      int candidateId = requiredIntText(
        requiredChild(*currentCandidate, "candidate_id",
          seatContext + "/first_preferences/candidate"),
        seatContext + "/first_preferences/candidate/candidate_id", 0);
      if (!candidates.contains(candidateId)) {
        currentCandidate = currentCandidate->NextSiblingElement("candidate");
        continue; // ignore candidates not in preload
      }
      auto const& pollingPlaces = requiredChild(*currentCandidate, "polling_places",
        seatContext + "/first_preferences/candidate");
      auto currentBooth = pollingPlaces.FirstChildElement("polling_place");
      while (currentBooth) {
        int boothId = integerTextOrZero(
          requiredChild(*currentBooth, "polling_place_id",
            seatContext + "/first_preferences/candidate/polling_places/polling_place"),
          seatContext + "/first_preferences/candidate/polling_places/polling_place/polling_place_id");
        std::string boothName = requiredText(
          requiredChild(*currentBooth, "polling_place_name",
            seatContext + "/first_preferences/candidate/polling_places/polling_place"),
          seatContext + "/first_preferences/candidate/polling_places/polling_place/polling_place_name");
        auto voteType = getBoothVoteTypeEcsa(boothName);
        int votes = integerTextOrZero(
          requiredChild(*currentBooth, "ballot_papers",
            seatContext + "/first_preferences/candidate/polling_places/polling_place"),
          seatContext + "/first_preferences/candidate/polling_places/polling_place/ballot_papers");
        if (voteType != VoteType::Ordinary) {
          seat.fpVotes[candidateId][getBoothVoteTypeEcsa(boothName)] += votes;
          currentBooth = currentBooth->NextSiblingElement("polling_place");
          continue; // Don't actually create booths for declaration votes
        }
        if (boothId == 0) boothId = generateBoothIdEcsa(seatId, boothName);
        if (!booths.contains(boothId)) {
          currentBooth = currentBooth->NextSiblingElement("polling_place");
          continue; // ignore booths not in preload
        }
        Booth& booth = booths[boothId];
        booth.fpVotes[candidateId] = votes;
        seat.fpVotes[candidateId][getBoothVoteTypeEcsa(booth.name)] += votes;
        currentBooth = currentBooth->NextSiblingElement("polling_place");
      }

      currentCandidate = currentCandidate->NextSiblingElement("candidate");
    }

    auto twoCandidatePreferred = currentDistrict->FirstChildElement("two_candidate_preferred");
    currentCandidate = twoCandidatePreferred ?
      twoCandidatePreferred->FirstChildElement("preferred_candidate") : nullptr;

    while (currentCandidate) {
      int candidateId = requiredIntText(
        requiredChild(*currentCandidate, "candidate_id",
          seatContext + "/two_candidate_preferred/preferred_candidate"),
        seatContext + "/two_candidate_preferred/preferred_candidate/candidate_id", 0);
      if (!candidates.contains(candidateId)) {
        currentCandidate = currentCandidate->NextSiblingElement("preferred_candidate");
        continue; // ignore candidates not in preload
      }
      int partyId = candidates.at(candidateId).party;
      auto const& pollingPlaces = requiredChild(*currentCandidate, "polling_places",
        seatContext + "/two_candidate_preferred/preferred_candidate");
      auto currentBooth = pollingPlaces.FirstChildElement("PreferredPollingPlace");
      while (currentBooth) {
        int boothId = integerTextOrZero(
          requiredChild(*currentBooth, "polling_place_id",
            seatContext + "/two_candidate_preferred/preferred_candidate/polling_places/PreferredPollingPlace"),
          seatContext + "/two_candidate_preferred/preferred_candidate/polling_places/PreferredPollingPlace/polling_place_id");
        std::string boothName = requiredText(
          requiredChild(*currentBooth, "polling_place_name",
            seatContext + "/two_candidate_preferred/preferred_candidate/polling_places/PreferredPollingPlace"),
          seatContext + "/two_candidate_preferred/preferred_candidate/polling_places/PreferredPollingPlace/polling_place_name");
        auto voteType = getBoothVoteTypeEcsa(boothName);
        int votes = integerTextOrZero(
          requiredChild(*currentBooth, "ballot_papers",
            seatContext + "/two_candidate_preferred/preferred_candidate/polling_places/PreferredPollingPlace"),
          seatContext + "/two_candidate_preferred/preferred_candidate/polling_places/PreferredPollingPlace/ballot_papers");
        if (voteType != VoteType::Ordinary) {
          seat.tcpVotes[partyId][getBoothVoteTypeEcsa(boothName)] += votes;
          seat.tcpVotesCandidate[candidateId][getBoothVoteTypeEcsa(boothName)] += votes;
          currentBooth = currentBooth->NextSiblingElement("PreferredPollingPlace");
          continue; // Don't actually create booths for declaration votes
        }
        if (boothId == 0) boothId = generateBoothIdEcsa(seatId, boothName);
        if (!booths.contains(boothId)) {
          currentBooth = currentBooth->NextSiblingElement("PreferredPollingPlace");
          continue; // ignore booths not in preload
        }
        Booth& booth = booths[boothId];
        booth.tcpVotes[partyId] = votes;
        booth.tcpVotesCandidate[candidateId] = votes;
        seat.tcpVotes[partyId][voteType] += votes;
        seat.tcpVotesCandidate[candidateId][voteType] += votes;
        currentBooth = currentBooth->NextSiblingElement("PreferredPollingPlace");
      }

      currentCandidate = currentCandidate->NextSiblingElement("preferred_candidate");
    }

    currentDistrict = currentDistrict->NextSiblingElement("district");
  }

  // temporary fix for "Greenwith West" TCP apparently flipped
  for (auto& [_, booth] : booths) {
    if (booth.name == "Greenwith West" && booth.tcpVotes.contains(2) && booth.tcpVotes[2] == 1026) {
      for (auto& [party, votes] : booth.tcpVotes) {
        if (votes == 1026) votes = 677;
        else if (votes == 677) votes = 1026;
      }
      for (auto& [party, votes] : booth.tcpVotesCandidate) {
        if (votes == 1026) votes = 677;
        else if (votes == 677) votes = 1026;
      }
    }
  }
  for (auto& [_, seat] : seats) {
    if (seat.name == "Narungga") {
      if (seat.fpVotes.contains(133004) && seat.fpVotes.at(133004).contains(VoteType::PrePoll) && seat.fpVotes.at(133004).at(VoteType::PrePoll) == 189) {
        // This crucial line in Narungga post-count is entered for the wrong parties, which messes up the projection
        // In future we should have a check for obviously-wrong entries, but for now just override it specifically
        // Some of these might not be in quite the right order, but it should avoid the massive error
        seat.fpVotes[133001][VoteType::PrePoll] = 189;
        seat.fpVotes[133002][VoteType::PrePoll] = 9;
        seat.fpVotes[133003][VoteType::PrePoll] = 95;
        seat.fpVotes[133004][VoteType::PrePoll] = 13;
        seat.fpVotes[133005][VoteType::PrePoll] = 153;
        seat.fpVotes[133006][VoteType::PrePoll] = 11;
        seat.fpVotes[133007][VoteType::PrePoll] = 311;
        seat.fpVotes[133008][VoteType::PrePoll] = 8;
        seat.fpVotes[133009][VoteType::PrePoll] = 13;
        seat.fpVotes[133010][VoteType::PrePoll] = 46;
      }
    }
  }

   //logger << "Ecsa Update for South Australia\n";
   //PA_LOG_VAR(booths.size());
   //logger << "==SEATS==\n";
   //for (auto const& [seatId, seat] : seats) {
   // PA_LOG_VAR(seat.name);
   // PA_LOG_VAR(seat.id);
   // PA_LOG_VAR(seat.enrolment);
   // PA_LOG_VAR(seat.fpVotes);
   // PA_LOG_VAR(seat.tcpVotes);
   // PA_LOG_VAR(seat.tcpVotesCandidate);
   // PA_LOG_VAR(seat.tppVotes);
   // PA_LOG_VAR(seat.booths);
   // for (int boothId : seat.booths) {
   //   PA_LOG_VAR(boothId);
   //   auto const& booth = booths.at(boothId);
   //   PA_LOG_VAR(booth.id);
   //   PA_LOG_VAR(booth.name);
   //   PA_LOG_VAR(booth.fpVotes);
   //   PA_LOG_VAR(booth.tcpVotes);
   //   PA_LOG_VAR(booth.tcpVotesCandidate);
   //   PA_LOG_VAR(booth.type);
   // }
   //}
   //logger << "==CANDIDATES==\n";
   //for (auto const& candidate : candidates) {
   // PA_LOG_VAR(candidate.second.id);
   // PA_LOG_VAR(candidate.second.name);
   // if (candidate.second.party != -1) {
   //   PA_LOG_VAR(parties[candidate.second.party].name);
   // }
   // else {
   //   logger << "Independent\n";
   // }
   //}
   //logger << "==PARTIES==\n";
   //for (auto const& party : parties) {
   // PA_LOG_VAR(party.second.id);
   // PA_LOG_VAR(party.second.name);
   // PA_LOG_VAR(party.second.shortCode);
   //}
  logger << "Appeared to successfully load election data update\n";
}

void Results2::Election::updateAecPollingPlaces(tinyxml2::XMLDocument const& xml)
{
  auto const& mediaFeed = requiredChild(xml, "MediaFeed", "AEC polling-place data");
  auto const& districtList = requiredChild(mediaFeed, "PollingDistrictList",
    "AEC polling-place data/MediaFeed");
  auto currentDistrict = districtList.FirstChildElement("PollingDistrict");
  if (!currentDistrict) {
    throwInvalidXml("AEC polling-place data/MediaFeed/PollingDistrictList",
      "missing <PollingDistrict>");
  }
  while (currentDistrict) {
    auto const& pollingPlaces = requiredChild(*currentDistrict, "PollingPlaces",
      "AEC polling-place data/PollingDistrict");
    auto currentPollingPlace = pollingPlaces.FirstChildElement("PollingPlace");
    while (currentPollingPlace) {
      auto locationEl = currentPollingPlace->FirstChildElement("eml:PhysicalLocation");
      auto locationId = locationEl ? locationEl->FindAttribute("Id") : nullptr;
      auto addressEl = locationEl ? locationEl->FirstChildElement("eml:Address") : nullptr;
      auto coordsEl = addressEl ? addressEl->FirstChildElement("xal:PostalServiceElements") : nullptr;
      auto latitudeEl = coordsEl ? coordsEl->FirstChildElement("xal:AddressLatitude") : nullptr;
      auto longitudeEl = coordsEl ? coordsEl->FirstChildElement("xal:AddressLongitude") : nullptr;
      if (locationId && latitudeEl && longitudeEl) {
        int boothId = requiredIntAttribute(*locationEl, "Id",
          "AEC polling-place data/PollingDistrict/PollingPlaces/PollingPlace/eml:PhysicalLocation", 0);
        if (booths.contains(boothId)) {
          booths[boothId].coords = {
            requiredFloatText(*latitudeEl,
              "AEC polling-place data/PollingDistrict/PollingPlaces/PollingPlace/latitude",
              -90.0f, 90.0f),
            requiredFloatText(*longitudeEl,
              "AEC polling-place data/PollingDistrict/PollingPlaces/PollingPlace/longitude",
              -180.0f, 180.0f)
          };
        }
      }
      currentPollingPlace = currentPollingPlace->NextSiblingElement("PollingPlace");
    }
    currentDistrict = currentDistrict->NextSiblingElement("PollingDistrict");
  }

}

void Results2::Election::applyResultOverrides() {
  std::string fileName = "analysis/Live Overrides/" + termCode + ".csv";
  auto file = std::ifstream(fileName);
  if (!file) {
    // Not finding a file is fine, but log a message in case this isn't intended behaviour
    logger << "Info: Could not find file " + fileName + " - original results will be used\n";
    return;
  }
  do {
    std::string line;
    std::getline(file, line);
    if (!file) break;
    auto values = splitString(line, ",");
    if (values.size() <= 1) break;
    if (values[0] == "tcp") {
      if (values.size() < 7) {
        logger << "Warning: Invalid line in " + fileName + ": " + line + "\n";
        continue;
      }
      std::string seatName = values[1];
      std::string boothName = values[2];
      std::string partyOneCode = values[3];
      std::string partyTwoCode = values[4];
      int partyOneVotes = std::stoi(values[5]);
      int partyTwoVotes = std::stoi(values[6]);
      auto booth = std::find_if(booths.begin(), booths.end(), [this, &seatName, &boothName](decltype(booths)::value_type const& b) {
        return b.second.name == boothName && seats.at(b.second.parentSeat).name == seatName;
      });
      if (booth == booths.end()) {
        logger << "Warning: Could not find booth " + boothName + " in seat " + seatName + " in " + fileName + "\n";

        std::vector<std::string> boothNames;
        for (auto const& b : booths) {
          if (seats.at(b.second.parentSeat).name == seatName) {
            boothNames.push_back(b.second.name);
          }
        }
        continue;	
      }
      auto candidateOne = std::find_if(booth->second.fpVotes.begin(), booth->second.fpVotes.end(),
        [this, &partyOneCode](decltype(booth->second.fpVotes)::value_type const& c) {
          return parties.at(candidates.at(c.first).party).shortCode == partyOneCode;
        }
      );
      auto candidateTwo = std::find_if(booth->second.fpVotes.begin(), booth->second.fpVotes.end(),
        [this, &partyTwoCode](decltype(booth->second.fpVotes)::value_type const& c) {
          return parties.at(candidates.at(c.first).party).shortCode == partyTwoCode;
        }
      );
      if (candidateOne == booth->second.fpVotes.end() || candidateTwo == booth->second.fpVotes.end()) {
        logger << "Warning: Could not find candidates for " + partyOneCode + " or " + partyTwoCode + " in " + fileName + "\n";
        continue;
      }
      booths.at(booth->first).tcpVotes[candidates.at(candidateOne->first).party] = partyOneVotes;
      booths.at(booth->first).tcpVotes[candidates.at(candidateTwo->first).party] = partyTwoVotes;
      booths.at(booth->first).tcpVotesCandidate[candidateOne->first] = partyOneVotes;
      booths.at(booth->first).tcpVotesCandidate[candidateTwo->first] = partyTwoVotes;
      logger << "Applied override for " + seatName + " " + boothName + " - " + partyOneCode + " "  + std::to_string(partyOneVotes) + " " + partyTwoCode + " " + std::to_string(partyTwoVotes) + "\n";
    }
  } while (true);
}
