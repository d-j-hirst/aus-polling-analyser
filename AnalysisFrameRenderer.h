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

class ElectionAnalyser;

class AnalysisFrameRenderer {
public:
	AnalysisFrameRenderer(PollingProject const& project, wxDC& dc, ElectionAnalyser const& electionAnalyser, wxSize dimensions, wxPoint textOffset);

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

	// sets the brush and pen to a particular colour.
	void setBrushAndPen(wxColour currentColour) const;

	void drawBackground() const;

	void drawText() const;

	float backgroundHeight() const;

	// defines the basic variables that represent the pixel limits of the graph.
	void defineBackgroundLimits();

	PollingProject const& project;
	wxDC& dc;
	ElectionAnalyser const& electionAnalyser;
	wxPoint textOffset;

	DisplayVariables dv;
};