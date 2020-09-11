#include "AnalysisFrameRenderer.h"

#include "ElectionAnalyser.h"

static int debugConnectionsToShow = 0;
constexpr int ClusterHeight = 18;
constexpr int SecondaryElectionWidth = 4;

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
	dc.SetBackground(wxBrush(wxColour(70, 220, 175)));
	dc.Clear();
}

void AnalysisFrameRenderer::setBrushAndPen(wxColour currentColour) const {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

void AnalysisFrameRenderer::render() {
	drawBackground();
	if (electionAnalyser.hasClusterAnalysis()) {
		drawClusters();
	}
	else {
		drawText();
	}
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

void drawCluster(wxDC& dc, ClusterAnalyser::Output const& data, ClusterAnalyser::Output::Cluster const& clusterData, wxPoint position, int boxWidth) {
	int electionNum = 0;
	for (auto const [electionKey, electionName] : data.electionNames) {
		//if (clusterData.elections.count(electionKey) && clusterData.elections.at(electionKey).greenFp) {
		if (clusterData.elections.count(electionKey) && clusterData.elections.at(electionKey).alp2cp) {
		//if (clusterData.elections.count(electionKey) && clusterData.elections.at(electionKey).alpSwing) {
			//constexpr float SwingColorSaturation = 0.1f;
			//float colorFactor = (clusterData.elections.at(electionKey).alpSwing.value() + SwingColorSaturation)
			//	/ (SwingColorSaturation * 2.0f);
			float colorFactor = (clusterData.elections.at(electionKey).alp2cp.value() - 0.2f)
				/ (0.6f);
			int red = std::clamp(int(colorFactor * 512.0f), 0, 255);
			int green = std::clamp(int(256.0f - abs(colorFactor - 0.5f) * 512.0f), 0, 255);
			int blue = std::clamp(int(512.0f - colorFactor * 512.0f), 0, 255);

			//float colorFactor = (clusterData.elections.at(electionKey).greenFp.value())
			//	/ (0.3f);					
			//int red = std::clamp(int(255.0f - colorFactor * 255.0f), 0, 255);
			//int green = 255;
			//int blue = std::clamp(int(255.0f - colorFactor * 255.0f), 0, 255);

			dc.SetBrush(wxBrush(wxColour(red, green, blue)));
		}
		else {
			dc.SetBrush(wxBrush(wxColour(128, 128, 128)));
		}
		dc.DrawRectangle(wxRect(position.x + electionNum * boxWidth,
			position.y, boxWidth + 1, ClusterHeight));
		++electionNum;
	}
}

void AnalysisFrameRenderer::drawClusters() const {
	if (electionAnalyser.clusterOutput().clusters.size()) {
		dc.SetPen(wxPen(wxColour(0, 0, 0)));
		auto const& data = electionAnalyser.clusterOutput();
		int primaryCluster = int(data.clusters.size() - 1);
		int currentCluster = primaryCluster;
		while (data.clusters[currentCluster].children.first >= 0) {
			currentCluster = data.clusters[currentCluster].children.first;
		}
		int clusterNum = 0;
		constexpr int ClusterLabelWidth = 300.0f;
		std::map<int, int> clusterOrder;
		int electionBoxWidth = data.electionNames.size() * ClusterHeight;
		int electionBoxWidthSecondary = data.electionNames.size() * SecondaryElectionWidth;
		while (true) {
			bool draw = textOffset.y + clusterNum * ClusterHeight > -100 && textOffset.y + clusterNum * ClusterHeight < 2000;
			auto const& clusterData = data.clusters[currentCluster];
			wxPoint position = textOffset + wxPoint(0, clusterNum * ClusterHeight);
			if (draw) drawCluster(dc, data, clusterData, position, ClusterHeight);
			wxRect nameRect = wxRect(textOffset.x + electionBoxWidth,
				textOffset.y + clusterNum * ClusterHeight, ClusterLabelWidth, ClusterHeight);
			if (draw) dc.DrawLabel(ClusterAnalyser::clusterName(clusterData, data), nameRect, wxALIGN_LEFT);
			clusterOrder[currentCluster] = clusterNum;
			while (data.clusters[currentCluster].parent >= 0 &&
				currentCluster == data.clusters[data.clusters[currentCluster].parent].children.second) {
				currentCluster = data.clusters[currentCluster].parent;
			}
			if (data.clusters[currentCluster].parent == -1) break;
			currentCluster = data.clusters[data.clusters[currentCluster].parent].children.second;
			while (data.clusters[currentCluster].children.first >= 0) {
				currentCluster = data.clusters[currentCluster].children.first;
			}
			++clusterNum;
		}
		constexpr int ConnectionWidth = 6;
		std::map<int, wxPoint> clusterPosition;
		int baseClusterX = ClusterLabelWidth + ClusterHeight * int(data.electionNames.size());
		// these will be in order of cluster generation
		for (int thisCluster = 0; thisCluster < int(data.clusters.size()); ++thisCluster) {
			if (data.clusters[thisCluster].children.first == -1) {
				clusterPosition[thisCluster].x = baseClusterX;
				clusterPosition[thisCluster].y = clusterOrder[thisCluster] * ClusterHeight + ClusterHeight / 2;
			}
			else if (data.clusters[data.clusters[thisCluster].children.first].children.first == -1) {
				clusterPosition[thisCluster].x = std::max(clusterPosition[data.clusters[thisCluster].children.first].x,
					clusterPosition[data.clusters[thisCluster].children.second].x) + ConnectionWidth;
				clusterPosition[thisCluster].y = (clusterPosition[data.clusters[thisCluster].children.first].y +
					clusterPosition[data.clusters[thisCluster].children.second].y) / 2;
			}
			else {
				clusterPosition[thisCluster].x = std::max(clusterPosition[data.clusters[thisCluster].children.first].x,
					clusterPosition[data.clusters[thisCluster].children.second].x) + ConnectionWidth + electionBoxWidthSecondary;
				clusterPosition[thisCluster].y = (clusterPosition[data.clusters[thisCluster].children.first].y +
					clusterPosition[data.clusters[thisCluster].children.second].y) / 2;
			}
		}
		for (int thisCluster = 0; thisCluster < int(data.clusters.size()); ++thisCluster) {
			if (data.clusters[thisCluster].children.first != -1) {
				bool noClusterInfo1 = data.clusters[data.clusters[thisCluster].children.first].children.first == -1;
				bool noClusterInfo2 = data.clusters[data.clusters[thisCluster].children.second].children.first == -1;
				wxPoint parent = clusterPosition[thisCluster];
				wxPoint child1 = clusterPosition[data.clusters[thisCluster].children.first];
				wxPoint child2 = clusterPosition[data.clusters[thisCluster].children.second];
				if (child2.y + textOffset.y < -100 || child1.y + textOffset.y > 2000) continue; // avoid even attempting to draw elements for offscreen
				auto const& clusterData = data.clusters[thisCluster];
				wxPoint position = parent + textOffset + wxPoint(0, -ClusterHeight / 2);
				dc.SetPen(wxPen(wxColour(0, 0, 0)));
				drawCluster(dc, data, clusterData, position, SecondaryElectionWidth);
				float colorFactor = data.clusters[thisCluster].similarity * 0.008f;
				int colorVal = std::clamp(int(colorFactor * 255.0f), 0, 255);
				dc.SetPen(wxPen(wxColour(colorVal, 0, 0)));
				wxPoint clusterOffset1 = wxPoint(noClusterInfo1 ? 0 : electionBoxWidthSecondary, 0);
				wxPoint clusterOffset2 = wxPoint(noClusterInfo2 ? 0 : electionBoxWidthSecondary, 0);
				dc.DrawLine(textOffset + child1 + clusterOffset1, textOffset + wxPoint(parent.x, child1.y));
				dc.DrawLine(textOffset + child2 + clusterOffset2, textOffset + wxPoint(parent.x, child2.y));
				dc.DrawLine(textOffset + wxPoint(parent.x, child1.y), textOffset + wxPoint(parent.x, child2.y));
			}
		}
	}
}

float AnalysisFrameRenderer::backgroundHeight() const
{
	return dv.DCheight - dv.displayTop;
}

void AnalysisFrameRenderer::defineBackgroundLimits() {

	dv.displayBottom = dv.DCheight;
	dv.displayTop = 0;
}