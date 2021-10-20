#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

// Results for an orthogonal regression with formula ax + by = c
struct OrthogonalRegression {
    float a;
    float b;
    float c;
    float rmse;
    int n;

    std::string report() {
        std::stringstream ss;
        ss << a << "x " << (b >= 0 ? "+ " : "- ") << std::abs(b) << "y = " << c;
        ss << ", n: " << n << ", rmse: " << rmse;
        return ss.str();
    }

    float solveForX(float y) {
        if (std::isnan(a)) return a;
        if (a == 0) return std::numeric_limits<float>::quiet_NaN();
        return (c - b * y) / a;
    }

    float solveForY(float x) {
        if (std::isnan(a)) return a;
        if (b == 0) return std::numeric_limits<float>::quiet_NaN();
        return (c - a * x) / b;
    }
};

inline OrthogonalRegression calculateOrthogonalRegression(std::vector<float> const& xValues, std::vector<float> const& yValues)
{
    OrthogonalRegression reg;
    reg.a = 0.0f;
    reg.b = 0.0f;
    reg.c = 0.0f;

    reg.n = int(std::min(xValues.size(), yValues.size()));
    float sX = 0;
    float sY = 0;
    float mX = 0;
    float mY = 0;
    float sXX = 0.0;
    float sXY = 0.0;
    float sYY = 0.0;

    //Calculate sums of X and Y
    for (int pair = 0; pair < reg.n; ++pair) {
         sX += xValues[pair];
         sY += yValues[pair];
    }

    //Calculate X and Y means (sample means)
    mX = sX / float(reg.n);
    mY = sY / float(reg.n);

    //Calculate sum of X squared, sum of Y squared and sum of X * Y
    //(components of the scatter matrix)
    for (int pair = 0; pair < reg.n; ++pair) {
        sXX += (xValues[pair] - mX) * (xValues[pair] - mX);
        sXY += (xValues[pair] - mX) * (yValues[pair] - mY);
        sYY += (yValues[pair] - mY) * (yValues[pair] - mY);
    }

    bool isVertical = sXY == 0 && sXX < sYY;
    bool isHorizontal = sXY == 0 && sXX > sYY;
    bool isIndeterminate = sXY == 0 && sXX == sYY;
    float slope = std::numeric_limits<float>::quiet_NaN();
    float intercept = std::numeric_limits<float>::quiet_NaN();

    if (isVertical)
    {
        reg.a = 1.0f;
        reg.b = 0.0f;
        reg.c = mX;
    }
    else if (isHorizontal)
    {
        reg.a = 0.0f;
        reg.b = 1.0f;
        reg.c = mY;
    }
    else if (isIndeterminate)
    {
        reg.a = std::numeric_limits<float>::quiet_NaN();
        reg.b = std::numeric_limits<float>::quiet_NaN();
        reg.c = std::numeric_limits<float>::quiet_NaN();
    }
    else
    {
        slope = (sYY - sXX + std::sqrt((sYY - sXX) * (sYY - sXX) + 4.0f * sXY * sXY)) / (2.0f * sXY);
        intercept = mY - slope * mX;
        float normFactor = (intercept >= 0.0f ? 1.0f : -1.0f) * std::sqrt(slope * slope + 1.0f);
        reg.a = -slope / normFactor;
        reg.b = 1.0f / normFactor;
        reg.c = intercept / normFactor;
    }

    float sumDistanceSquared = 0.0f;
    for (int pair = 0; pair < reg.n; ++pair) {
        float distanceSquared = float(std::pow(reg.a * xValues[pair] + reg.b * xValues[pair] + reg.c, 2))
            / (reg.a * reg.a + reg.b * reg.b);
        sumDistanceSquared += distanceSquared;
    }

    reg.rmse = std::sqrt(sumDistanceSquared);

    return reg;
}

// Calculate orthogonal regression between the matching pairs in xValues and yValues
// where a matching pair consists of the values in each map that share the same key
inline OrthogonalRegression calculateOrthogonalRegression(std::map<int, float> const& xValues, std::map<int, float> const& yValues) {
    std::vector<float> xVector;
    std::vector<float> yVector;
    for (auto [key, val] : xValues) {
        if (yValues.count(key)) {
            xVector.push_back(val);
            yVector.push_back(yValues.at(key));
        }
    }
    return calculateOrthogonalRegression(xVector, yVector);
}