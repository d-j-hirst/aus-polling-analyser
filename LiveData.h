#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Portable boundary between the core simulation and a live-results analysis
// implementation. These types deliberately have no dependency on electoral-
// commission parsers, wxWidgets, or third-party libraries.
namespace LiveData {

// Numeric values preserve the existing Results2::VoteType representation used
// in saved project files.
enum class VoteType : int {
	Invalid = 0,
	Ordinary = 1,
	Absent = 2,
	Provisional = 3,
	PrePoll = 4,
	Postal = 5,
	Early = 6,
	IVote = 7,
	Telephone = 8,
	SIR = 9,
	EarlyProvisional = 10,
	EVM = 11,
	TIO = 12,
};

// Numeric values preserve the existing Results2::Booth::Type representation.
enum class BoothType : int {
	Normal = 0,
	Ppvc = 1,
	Remote = 2,
	Prison = 3,
	Hospital = 4,
	Other = 5,
	Invalid = 6,
};

constexpr std::string_view voteTypeName(VoteType voteType)
{
	switch (voteType) {
	case VoteType::Ordinary: return "Ordinary";
	case VoteType::Absent: return "Absent";
	case VoteType::Provisional: return "Provisional";
	case VoteType::PrePoll: return "PrePoll";
	case VoteType::Postal: return "Postal";
	case VoteType::Early: return "Early";
	case VoteType::IVote: return "iVote";
	case VoteType::Telephone: return "Telephone";
	case VoteType::SIR: return "SIR";
	case VoteType::EarlyProvisional: return "Early Provisional";
	case VoteType::EVM: return "EVM";
	case VoteType::TIO: return "TIO";
	case VoteType::Invalid: return "Invalid";
	}
	return "Invalid";
}

constexpr std::string_view boothTypeName(BoothType boothType)
{
	switch (boothType) {
	case BoothType::Normal: return "Normal";
	case BoothType::Ppvc: return "PPVC";
	case BoothType::Remote: return "Remote";
	case BoothType::Prison: return "Prison";
	case BoothType::Hospital: return "Hospital";
	case BoothType::Other: return "Other";
	case BoothType::Invalid: return "Invalid";
	}
	return "Invalid";
}

struct ValueInformation {
	float value = 0.0f;
	float completion = 0.0f;
	float confidence = 0.0f;
};

struct BaselineInformation {
	float baseline = 0.0f;
	float completion = 0.0f;
	float confidence = 0.0f;
	float deviation = 0.0f;
};

struct TcpShareInformation {
	std::map<int, float> shares;
	float completion = 0.0f;
	float confidence = 0.0f;
};

// Diagnostic values used by the live summary export. They remain DTOs rather
// than exposing the live analyser's internal node hierarchy.
struct Internals {
	std::map<BoothType, float> boothTypeBiases;
	std::map<VoteType, float> voteTypeBiases;
	std::map<BoothType, float> boothTypeBiasStdDev;
	std::map<VoteType, float> voteTypeBiasStdDev;
	std::map<BoothType, float> boothTypeBiasesRaw;
	std::map<VoteType, float> voteTypeBiasesRaw;
	std::map<BoothType, float> boothTypeSourceCount;
	std::map<VoteType, float> voteTypeSourceCount;
	std::map<BoothType, float> boothTypeVoteCount;
	std::map<VoteType, float> voteTypeVoteCount;
	float projected2pp = 0.0f;
	float raw2ppDeviation = 0.0f;
};

struct BoothSnapshot {
	std::string seatName;
	std::string boothName;
	BoothType boothType = BoothType::Invalid;
	VoteType voteType = VoteType::Invalid;
	bool sameSeat = false;
};

// Read-only contract consumed by simulation iterations, completion reporting,
// and live-result GUI views. Implementations own their internal data and return
// an independently owned provider for each stochastic scenario.
class Provider {
public:
	virtual ~Provider() = default;

	virtual std::unique_ptr<Provider> generateScenario(
		int iterationIndex) const = 0;

	virtual BaselineInformation getFinalSpecificTppInformation() const = 0;
	virtual std::map<int, float> getFinalSpecificFpDeviations() const = 0;
	virtual float getRegionFinalSpecificTppDeviation(int regionIndex) const = 0;
	virtual std::map<int, float> getRegionFinalSpecificFpDeviations(
		int regionIndex) const = 0;
	virtual BaselineInformation getSeatTppInformation(
		std::string const& seatName) const = 0;
	virtual std::map<int, BaselineInformation> getSeatFpInformation(
		std::string const& seatName) const = 0;
	virtual ValueInformation getSeatOthersInformation(
		std::string const& seatName,
		std::map<int, float> const& representedParties) const = 0;
	virtual TcpShareInformation getSeatTcpInformation(
		std::string const& seatName) const = 0;
	virtual std::optional<ValueInformation> getSeatNationalsProportion(
		std::string const& seatName) const = 0;

	virtual float getSeatRawTppSwing(std::string const& seatName) const = 0;
	virtual float getSeatFpCompletion(std::string const& seatName) const = 0;
	virtual float getSeatTppCompletion(std::string const& seatName) const = 0;
	virtual float getSeatTcpCompletion(std::string const& seatName) const = 0;

	virtual Internals getInternals() const = 0;
	virtual std::vector<BoothSnapshot> getBoothSnapshots() const = 0;
};

} // namespace LiveData
