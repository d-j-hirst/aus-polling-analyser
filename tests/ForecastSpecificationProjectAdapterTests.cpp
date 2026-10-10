#include "ForecastSpecificationIO.h"
#include "ForecastSpecificationProjectAdapter.h"
#include "General.h"
#include "PollingProject.h"
#include "WorkspacePaths.h"

#include <filesystem>
#include <iostream>
#include <cmath>
#include <string>

namespace {
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

	bool checkVictorianCoalition(PollingProject const& project)
	{
		// These are configuration checks, using the known 2022 candidate roster.
		// Both-party contests must remain possible, while Nationals-only seats
		// must not acquire a fictional Liberal candidate.
		auto const& parties = project.parties();
		auto const national = parties.separateNationalsIndex();
		if (parties.count() != 8 || national != 7 ||
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
					seat.candidateNames.at("Paul Matheson") != "LNP")) return false;
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
		auto const& project = *constructed.project;
		if (!checkNationalsLookup(*constructed.project)) {
			std::cerr << "Combined and separate Nationals lookups disagree\n";
			valid = false;
		}
		if (specification.electionCode == "2022vic" &&
			!checkVictorianCoalition(project)) {
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
