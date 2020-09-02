#include "AnalysisFrameRenderer.h"

#include "ElectionAnalyser.h"

inline wxFont font(int fontSize) {
	return wxFont(fontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
}

AnalysisFrameRenderer::AnalysisFrameRenderer(PollingProject const& project, wxDC& dc, ElectionAnalyser const& electionAnalyser, wxSize dimensions, wxPoint textOffset)
	: project(project), dc(dc), electionAnalyser(electionAnalyser), textOffset(textOffset)
{
	dv.DCwidth = dimensions.GetWidth();
	dv.DCheight = dimensions.GetHeight();
}

void AnalysisFrameRenderer::clearDC(wxDC& dc)
{
	dc.SetBackground(wxBrush(wxColour(70, 255, 255)));
	dc.Clear();
}

void AnalysisFrameRenderer::setBrushAndPen(wxColour currentColour) const {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

void AnalysisFrameRenderer::render() {
	drawBackground();
	drawText();
	// do stuff
}

void AnalysisFrameRenderer::drawBackground() const
{
}

void AnalysisFrameRenderer::drawText() const
{
	std::string text = electionAnalyser.textOutput();
	wxRect rect = wxRect(textOffset.x, textOffset.y, 4000, 4000);
	dc.DrawLabel(text, rect, wxALIGN_LEFT);
}

float AnalysisFrameRenderer::backgroundHeight() const
{
	return dv.DCheight - dv.displayTop;
}

void AnalysisFrameRenderer::defineBackgroundLimits() {

	dv.displayBottom = dv.DCheight;
	dv.displayTop = 0;
}