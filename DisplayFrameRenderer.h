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
#include "PollingProject.h"

class DisplayFrameRenderer {
public:
	DisplayFrameRenderer(PollingProject const& project, wxDC& dc, Simulation const& simulation, wxSize dimensions);

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

	void drawRegionsBoxRowListItem(Region const& thisRegion, RegionCollection::Index regionIndex, wxRect rowNameRect) const;

	void drawBoundsBox() const;

	void drawBoundsBoxBackground() const;

	void drawBoundsBoxTitle() const;

	void drawBoundsBoxColumnHeadings() const;

	void drawBoundsBoxItems() const;

	void drawGraphBox() const;

	void drawGraphBoxBackground() const;

	void drawGraphAxisLabels() const;

	void drawGraphColumns() const;

	void drawGraphAxis() const;

	void drawSeatsBox() const;

	// defines the basic variables that represent the pixel limits of the graph.
	void defineGraphLimits();

	// sets the brush and pen to a particular colour.
	void setBrushAndPen(wxColour currentColour) const;

	float backgroundHeight() const;

	float probabilityBoxTop() const;

	float expectationBoxTop() const;

	PollingProject const& project;
	wxDC& dc;
	Simulation const& simulation;

	DisplayVariables dv;
};