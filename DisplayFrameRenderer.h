#pragma once

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers)
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <memory>
#include "Simulation.h"

class DisplayFrameRenderer {
public:
	DisplayFrameRenderer(wxDC& dc, Simulation::Report const& simulation, wxSize dimensions);

	void render();

	// clears the drawing area.
	static void clearDC(wxDC& dc);
private:

	struct DisplayVariables {
		float DCwidth;
		float DCheight;
		float displayTop;
		float displayBottom;
	};

	struct GraphVariables {
		int lowestSeatFrequency;
		int highestSeatFrequency;
		int seatRange;
		float GraphBoxHeight;
		float GraphBoxTop;
		float axisY;
		float axisLeft;
		float axisRight;
		float axisMidpoint;
		float axisLength;
		int seatColumnWidth;
	};

	void drawBackground() const;

	void drawProbabilityBox() const;

	wxColour lightenedPartyColour(Party::Id partyId) const;

	void drawProbabilityBoxBackground() const;

	void drawProbabilityBoxLabels() const;

	void drawProbabilityBoxData() const;

	void drawProbabilityBoxText(wxRect& rect, std::string const& text, wxPoint subsequentOffset) const;

	void drawSumOfLeads() const;

	void drawSumOfLeadsText(wxRect& outerRect, std::string const& annotationText, std::string const& dataText) const;

	void drawExpectationsBox() const;

	void drawExpectationsBoxBackground() const;

	void drawExpectationsBoxTitle() const;

	void drawExpectationsBoxRows() const;

	void drawExpectationsBoxRow(wxRect& nameRect, PartyCollection::Index partyIndex) const;

	void drawRegionsBox() const;

	void drawRegionsBoxBackground() const;

	void drawRegionsBoxTitle() const;

	void drawRegionsBoxRows() const;

	void drawRegionsBoxRowTitles() const;

	void drawRegionsBoxRowList() const;

	void drawRegionsBoxRowListItem(RegionCollection::Index regionIndex, wxRect rowNameRect) const;

	void drawBoundsBox() const;

	void drawBoundsBoxBackground() const;

	void drawBoundsBoxTitle() const;

	void drawBoundsBoxColumnHeadings() const;

	void drawBoundsBoxItems() const;

	void drawGraphBox() const;

	GraphVariables calculateGraphVariables() const;

	void drawGraphBoxBackground(GraphVariables const& gv) const;

	void drawGraphAxisLabels(GraphVariables const& gv) const;

	void drawGraphColumns(GraphVariables const& gv) const;

	void drawGraphAxis(GraphVariables const& gv) const;

	void drawSeatsBox() const;

	void drawSeatsBoxBackground() const;

	void drawSeatsBoxTitle() const;

	void drawSeatsList() const;

	// defines the basic variables that represent the pixel limits of the graph.
	void defineGraphLimits();

	// sets the brush and pen to a particular colour.
	void setBrushAndPen(wxColour currentColour) const;

	float backgroundHeight() const;

	float probabilityBoxTop() const;

	float expectationBoxTop() const;

	wxDC& dc;
	Simulation::Report const& simulation;

	DisplayVariables dv;
};