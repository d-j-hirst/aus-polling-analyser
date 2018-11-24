#pragma once

enum FileSectionsEnum {
	FileSection_None,
	FileSection_Project,
	FileSection_Parties,
	FileSection_Pollsters,
	FileSection_Polls,
	FileSection_Events,
	FileSection_Models,
	FileSection_Projections,
	FileSection_Regions,
	FileSection_Seats,
	FileSection_Simulations,
	FileSection_Results
};

struct FileOpeningState {
	FileSectionsEnum section;
	int index;
};