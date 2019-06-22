#pragma once

constexpr float Pi_f = 3.14159f;
constexpr float TwoPi = Pi_f * 2.0f;
constexpr float OneOverTwoPi = 1.0f / TwoPi;

constexpr double Pi_d = 3.14159;
constexpr double TwoPi_d = Pi_d * 2.0;
constexpr double OneOverTwoPi_d = 1.0 / TwoPi_d;

constexpr int RevolutionDegrees = 360;
constexpr float RevolutionDegreesF = float(RevolutionDegrees);
constexpr double RevolutionDegreesD = double(RevolutionDegrees);
constexpr float RevolutionsPerDegreeF = 1.0f / RevolutionDegreesF;
constexpr double RevolutionsPerDegreeD = 1.0 / RevolutionDegreesD;

constexpr float RightAngleDegrees = RevolutionDegreesF * 0.25f;

template <typename T>
constexpr T toRadians(T degreesVal) {
	return degreesVal * T(RevolutionsPerDegreeD) * TwoPi;
}

template <typename T>
constexpr T toDegrees(T radiansVal) {
	return radiansVal * T(RevolutionDegrees) * OneOverTwoPi;
}