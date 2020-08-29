#include "ElectionAnalyser.h"

#include "Log.h"
#include "PartiesAnalyser.h"

ElectionAnalyser::ElectionAnalyser(ElectionCollection const& elections)
	: elections(elections)
{
}

void ElectionAnalyser::run(Type type, int electionFocus)
{
	if (type == Type::Parties) {
		PartiesAnalyser(elections).run(electionFocus);
	}
}
