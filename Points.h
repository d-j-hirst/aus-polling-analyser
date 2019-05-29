#pragma once

#include "Log.h"

#include <cmath>
#include <algorithm>
#include <math.h>
#include <string>

struct Point3D;
struct Point3Df;
struct Point2Df;
struct Point2D;

struct Point4D {
	constexpr Point4D(double x, double y, double z, double w) : x(x), y(y), z(z), w(w) {}
	constexpr Point4D() : x(0), y(0), z(0), w(0) {}
	double magnitude() const { return std::sqrt(x * x + y * y + z * z + w * w); }
	Point4D normalize() const { double mag = magnitude(); return Point4D(x / mag, y / mag, z / mag, w / mag); }
	constexpr double dot(Point4D other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }

	constexpr Point4D minimize(Point4D const& other) const {
		double new_x = std::min(x, other.x);
		double new_y = std::min(y, other.y);
		double new_z = std::min(z, other.z);
		double new_w = std::min(z, other.z);
		return Point4D(new_x, new_y, new_z, new_w);
	}

	constexpr Point4D maximize(Point4D const& other) const {
		double new_x = std::max(x, other.x);
		double new_y = std::max(y, other.y);
		double new_z = std::max(z, other.z);
		double new_w = std::max(w, other.w);
		return Point4D(new_x, new_y, new_z, new_w);
	}

	constexpr Point4D reciprocal() const { return Point4D(1.0 / x, 1.0 / y, 1.0 / z, 1.0 / w); }
	std::string stringify() const;
	double x;
	double y;
	double z;
	double w;
};

struct Point3D {
	constexpr Point3D(double x, double y, double z) : x(x), y(y), z(z) {}
	constexpr Point3D() : x(0), y(0), z(0) {}
	explicit Point3D(Point3Df basePoint);
    // Returns spherical coordinates (phi, theta) in degrees
	Point2Df getSphericalF() const;
    // Returns spherical coordinates (phi, theta) in degrees
	Point2D getSpherical() const;
	double magnitude() const { return std::sqrt(magnitudeSquared()); }
	double magnitudeSquared() const { return x * x + y * y + z * z; }
    // returns value in degrees
	double getLatitude() const { return (std::acos(y / magnitude()) - 3.14159265 * 0.5f) * 180 / 3.14159265; }
    // returns value in degrees
	double getLongitude() const { return std::atan2(z, x) * 180 / 3.14159265; }
	Point3D normalize() const { double mag = magnitude(); return Point3D(x / mag, y / mag, z / mag); }
    // Returns normalized magnitude and also writes the magnitude into the given reference variable
	Point3D normalizeMag (double& magnitudeOutput) const;
	Point3D floor() const { return Point3D(std::floor(x), std::floor(y), std::floor(z)); }
	constexpr double dot(Point3D other) const { return x * other.x + y * other.y + z * other.z; }

	constexpr Point3D cross(Point3D const& other) const {
		Point3D tempPoint;
		tempPoint.x = y * other.z - z * other.y;
		tempPoint.y = z * other.x - x * other.z;
		tempPoint.z = x * other.y - y * other.x;
		return tempPoint;
	}

	constexpr Point3D minimize(Point3D const& other) const {
		double new_x = std::min(x, other.x);
		double new_y = std::min(y, other.y);
		double new_z = std::min(z, other.z);
		return Point3D(new_x, new_y, new_z);
	}

	constexpr Point3D maximize(Point3D const& other) const {
		double new_x = std::max(x, other.x);
		double new_y = std::max(y, other.y);
		double new_z = std::max(z, other.z);
		return Point3D(new_x, new_y, new_z);
	}

	constexpr Point3D reciprocal() const { return Point3D(1.0 / x, 1.0 / y, 1.0 / z); }
	double distance(Point3D other) const;
	std::string stringify() const;
    double x;
    double y;
    double z;
};

inline Logger& operator<<(Logger& loggerToUse, const Point3D& obj) {
	loggerToUse << obj.stringify();
	return loggerToUse;
}

inline constexpr bool operator==(const Point3D& lhs, const Point3D& rhs) {
	return (lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z);
}

inline constexpr void operator+=(Point3D& lhs, const Point3D& rhs) {
	lhs.x += rhs.x;
	lhs.y += rhs.y;
	lhs.z += rhs.z;
}

inline constexpr Point3D operator+(Point3D lhs, const Point3D& rhs) {
	lhs += rhs;
	return lhs;
}

inline constexpr void operator-=(Point3D& lhs, const Point3D& rhs) {
	lhs.x -= rhs.x;
	lhs.y -= rhs.y;
	lhs.z -= rhs.z;
}

inline constexpr Point3D operator-(Point3D lhs, const Point3D& rhs) {
	lhs -= rhs;
	return lhs;
}

inline constexpr void operator*=(Point3D& lhs, const double& rhs) {
	lhs.x *= rhs;
	lhs.y *= rhs;
	lhs.z *= rhs;
}

inline constexpr Point3D operator*(Point3D lhs, const double& rhs) {
	lhs *= rhs;
	return lhs;
}

inline constexpr Point3D operator*(const double & lhs, Point3D rhs) {
	return rhs * lhs;
}

inline constexpr void operator/=(Point3D& lhs, const double& rhs) {
	lhs.x /= rhs;
	lhs.y /= rhs;
	lhs.z /= rhs;
}

inline constexpr Point3D operator/(Point3D lhs, const double& rhs) {
	lhs /= rhs;
	return lhs;
}

inline constexpr Point3D operator-(const Point3D & lhs) {
	return Point3D(-lhs.x, -lhs.y, -lhs.z);
}

struct Point3Df {
	constexpr Point3Df(float x, float y, float z) : x(x), y(y), z(z) {}
	constexpr Point3Df() : x(0.0f), y(0.0f), z(0.0f) {}
	Point3Df(Point2Df xy, float z);
	explicit Point3Df(Point3D basePoint);
    // Returns spherical coordinates (phi, theta) in degrees
	Point2Df getSphericalF() const;
    // Returns spherical coordinates (phi, theta) in degrees
	Point2D getSpherical() const;
	float magnitude() const { return std::sqrt(x * x + y * y + z * z); }
    // returns value in degrees
	float getLatitude() const { return (std::acos(y / magnitude()) - float(3.14159265) * 0.5f) * 180.0f / 3.14159265f; }
    // returns value in degrees
	float getLongitude() const { return std::atan2(z, x) * float(180) / float(3.14159265); }
	Point3Df normalize() const { float mag = magnitude(); return Point3Df(x / mag, y / mag, z / mag); }
	Point3Df floor() const { return Point3Df(std::floor(x), std::floor(y), std::floor(z)); }
	constexpr float dot(Point3Df other) const { return x * other.x + y * other.y + z * other.z; }

	constexpr Point3Df cross(Point3Df const& other) const {
		Point3Df tempPoint;
		tempPoint.x = y * other.z - z * other.y;
		tempPoint.y = z * other.x - x * other.z;
		tempPoint.z = x * other.y - y * other.x;
		return tempPoint;
	}

	constexpr Point3Df minimize(Point3Df const& other) const {
		float new_x = std::min(x, other.x);
		float new_y = std::min(y, other.y);
		float new_z = std::min(z, other.z);
		return Point3Df(new_x, new_y, new_z);
	}

	constexpr Point3Df maximize(Point3Df const& other) const {
		float new_x = std::max(x, other.x);
		float new_y = std::max(y, other.y);
		float new_z = std::max(z, other.z);
		return Point3Df(new_x, new_y, new_z);
	}

	constexpr Point3Df reciprocal() { return Point3Df(1.0f / x, 1.0f / y, 1.0f / z); }
	std::string stringify() const;
    float x;
    float y;
    float z;
};

inline Logger& operator<<(Logger& loggerToUse, const Point3Df& obj) {
	loggerToUse << obj.stringify();
	return loggerToUse;
}

inline constexpr bool operator==(const Point3Df& lhs, const Point3Df& rhs) {
	return (lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z);
}

inline constexpr void operator+=(Point3Df& lhs, const Point3Df& rhs) {
	lhs.x += rhs.x;
	lhs.y += rhs.y;
	lhs.z += rhs.z;
}

inline constexpr Point3Df operator+(Point3Df lhs, const Point3Df& rhs) {
	lhs += rhs;
	return lhs;
}

inline constexpr void operator-=(Point3Df& lhs, const Point3Df& rhs) {
	lhs.x -= rhs.x;
	lhs.y -= rhs.y;
	lhs.z -= rhs.z;
}

inline constexpr Point3Df operator-(Point3Df lhs, const Point3Df& rhs) {
	lhs -= rhs;
	return lhs;
}

inline constexpr void operator*=(Point3Df& lhs, const float& rhs) {
	lhs.x *= rhs;
	lhs.y *= rhs;
	lhs.z *= rhs;
}

inline constexpr Point3Df operator*(Point3Df lhs, const float& rhs) {
	lhs *= rhs;
	return lhs;
}

inline constexpr Point3Df operator*(const float & lhs, Point3Df rhs) {
	return rhs * lhs;
}

inline constexpr Point3Df operator-(const Point3Df & lhs) {
	return Point3Df(-lhs.x, -lhs.y, -lhs.z);
}

struct Point3Di {
	constexpr Point3Di(int x, int y, int z) : x(x), y(y), z(z) {}
	constexpr Point3Di() : x(0), y(0), z(0) {}
	explicit Point3Di(Point3D const& point) : x(int(std::floor(point.x))), y(int(floor(point.y))), z(int(floor(point.z))) {}
	explicit Point3Di(Point3Df const& point) : x(int(floor(point.x))), y(int(floor(point.y))), z(int(floor(point.z))) {}
	std::string stringify() const;
    int x;
    int y;
    int z;
};

inline constexpr bool operator==(const Point3Di& lhs, const Point3Di& rhs) {
	return (lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z);
}

inline constexpr void operator+=(Point3Di& lhs, const Point3Di& rhs) {
	lhs.x += rhs.x;
	lhs.y += rhs.y;
	lhs.z += rhs.z;
}

inline constexpr Point3Di operator+(Point3Di lhs, const Point3Di& rhs) {
	lhs += rhs;
	return lhs;
}

inline constexpr void operator-=(Point3Di& lhs, const Point3Di& rhs) {
	lhs.x -= rhs.x;
	lhs.y -= rhs.y;
	lhs.z -= rhs.z;
}

inline constexpr Point3Di operator-(Point3Di lhs, const Point3Di& rhs) {
	lhs -= rhs;
	return lhs;
}

inline constexpr void operator*=(Point3Di& lhs, const int& rhs) {
	lhs.x *= rhs;
	lhs.y *= rhs;
	lhs.z *= rhs;
}

inline constexpr Point3Di operator*(Point3Di lhs, const int& rhs) {
	lhs *= rhs;
	return lhs;
}

inline constexpr Point3Di operator*(const int & lhs, Point3Di rhs) {
	return rhs * lhs;
}

inline constexpr Point3Di operator-(const Point3Di & lhs) {
	return Point3Di(-lhs.x, -lhs.y, -lhs.z);
}

inline Logger& operator<<(Logger& loggerToUse, const Point3Di& obj) {
	loggerToUse << obj.stringify();
	return loggerToUse;
}

struct Point2Di {
    constexpr Point2Di(int in_x, int in_y) : x(in_x), y(in_y) {}
	constexpr explicit Point2Di(int in) : x(in), y(in) {}
	constexpr Point2Di() : x(0), y(0) {}
    explicit Point2Di(Point3D point) : x(int(floor(point.x))), y(int(floor(point.y))) {}
	constexpr int minimum() const { return std::min(x, y); }
	constexpr int maximum() const { return std::max(x, y); }
	constexpr Point2Di add_x(int xx) const { return Point2Di(x + xx, y); }
	constexpr Point2Di add_y(int yy) const { return Point2Di(x, y + yy); }
	constexpr Point2Di add(Point2Di point) const { return Point2Di(x + point.x, y + point.y); }
	constexpr Point2Di add(int xx, int yy) const { return Point2Di(x + xx, y + yy); }
	constexpr Point2Di subtract(Point2Di point) const { return Point2Di(x - point.x, y - point.y); }
	constexpr Point2Di subtract(int xx, int yy) const { return Point2Di(x - xx, y - yy); }
	constexpr Point2Di halve() const { return Point2Di(x / 2, y / 2); }
	constexpr bool isWithin(Point2Di lowPoint, Point2Di highPoint) const {
        if (x < lowPoint.x) return false;
        if (x > highPoint.x) return false;
        if (y < lowPoint.y) return false;
        if (y > highPoint.y) return false;
        return true;
    }
	constexpr int sum() const { return x + y; }
    int sumAbs() const { return abs(x) + abs(y); }
	constexpr bool isNonZero() const { if (x != 0 || y != 0) return true; return false; }
	std::string stringify() const;
    int x;
    int y;
};

inline Logger& operator<<(Logger& loggerToUse, const Point2Di& obj) {
	loggerToUse << obj.stringify();
	return loggerToUse;
}

inline constexpr bool operator==(const Point2Di& lhs, const Point2Di& rhs) {
	return (lhs.x == rhs.x && lhs.y == rhs.y);
}

inline constexpr void operator+=(Point2Di& lhs, const Point2Di& rhs) {
	lhs.x += rhs.x;
	lhs.y += rhs.y;
}

inline constexpr Point2Di operator-(const Point2Di& operand) {
	return Point2Di(-operand.x, -operand.y);
}

inline constexpr Point2Di operator+(Point2Di lhs, const Point2Di& rhs) {
	lhs += rhs;
	return lhs;
}

inline constexpr void operator-=(Point2Di& lhs, const Point2Di& rhs) {
	lhs.x -= rhs.x;
	lhs.y -= rhs.y;
}

inline constexpr Point2Di operator-(Point2Di lhs, const Point2Di& rhs) {
	lhs -= rhs;
	return lhs;
}

inline constexpr void operator*=(Point2Di& lhs, const int& rhs) {
	lhs.x *= rhs;
	lhs.y *= rhs;
}

inline constexpr Point2Di operator*(Point2Di lhs, const int& rhs) {
	lhs *= rhs;
	return lhs;
}

inline constexpr Point2Di operator*(const int& lhs, Point2Di rhs) {
	return rhs * lhs;
}

inline constexpr void operator/=(Point2Di & lhs, const int & rhs) {
	lhs.x /= rhs;
	lhs.y /= rhs;
}

inline constexpr Point2Di operator/(Point2Di lhs, const int & rhs) {
	lhs /= rhs;
	return lhs;
}

struct Point2Df {
	constexpr Point2Df(float x, float y) : x(x), y(y) {}
	explicit Point2Df(Point2Di point) : x(float(point.x)), y(float(point.y)) {}
	constexpr Point2Df() : x(0.0f), y(0.0f) {}
	constexpr Point2Df componentDivision(Point2Df const& divisor) const { return Point2Df(x / divisor.x, y / divisor.y); }
	constexpr Point2Df componentMultiplication(Point2Df const multiplier) const { return Point2Df(x * multiplier.x, y * multiplier.y); }
    float magnitude() const { return std::hypot(x, y); }
    // More efficient way of getting the squared magitude if that is usable
	constexpr float getMagnitudeSquared() const { return x * x + y * y; }
    Point2Df normalize() const { float mag = magnitude(); return Point2Df(x / mag, y / mag); }
	constexpr float dot(Point2Df other) const { return x * other.x + y * other.y; }
    // Inverse arctan function for determining the angle away from the x-axis
    float X_Axis_atan2() const { return std::atan2(y, x); }
    // Inverse arctan function for determining the angle away from the y-axis
    float Y_Axis_atan2() const { return std::atan2(x, y); }
    // Returns true if either component is not-a-number, and false otherwise
	constexpr double det(Point2Df const& otherPoint) const { return x * otherPoint.y - otherPoint.x * y; }
    bool hasNan() const { return std::isnan(x) || std::isnan(y); }
	constexpr bool isZero() const { return (x == 0 && y == 0); }
    double distance(Point2Df other) const;
	std::string stringify() const;
	// returns the point's coordinates scales so that xBound and yBound
	// represent the new 0-1 values of returned coordinates
	Point2Df scale(Point2Df zeroPoint, Point2Df onePoint);
	// New point with component-wise minima of this and the other given point
	Point2Df min(Point2Df otherPoint) { return Point2Df(std::min(x, otherPoint.x), std::min(y, otherPoint.y)); }
	// New point with component-wise maxima of this and the other given point
	Point2Df max(Point2Df otherPoint) { return Point2Df(std::max(x, otherPoint.x), std::max(y, otherPoint.y)); }
    float x;
    float y;
};

inline Logger& operator<<(Logger& loggerToUse, const Point2Df& obj) {
	loggerToUse << obj.stringify();
	return loggerToUse;
}

inline constexpr bool operator==(const Point2Df& lhs, const Point2Df& rhs) {
	return (lhs.x == rhs.x && lhs.y == rhs.y);
}

inline constexpr void operator+=(Point2Df& lhs, const Point2Df& rhs) {
	lhs.x += rhs.x;
	lhs.y += rhs.y;
}

inline constexpr Point2Df operator+(Point2Df lhs, const Point2Df& rhs) {
	lhs += rhs;
	return lhs;
}

inline constexpr void operator-=(Point2Df& lhs, const Point2Df& rhs) {
	lhs.x -= rhs.x;
	lhs.y -= rhs.y;
}

inline constexpr Point2Df operator-(Point2Df lhs, const Point2Df& rhs) {
	lhs -= rhs;
	return lhs;
}

inline constexpr void operator*=(Point2Df& lhs, const float& rhs) {
	lhs.x *= rhs;
	lhs.y *= rhs;
}

inline constexpr Point2Df operator*(Point2Df lhs, const float& rhs) {
	lhs *= rhs;
	return lhs;
}

inline constexpr Point2Df operator*(const float& lhs, Point2Df rhs) {
	return rhs * lhs;
}

struct Point2D {
	constexpr Point2D(double in_x, double in_y) : x(in_x), y(in_y) {}
	constexpr Point2D() : x(0), y(0) {}

	constexpr Point2D componentDivision(Point2D & divisor) { return Point2D(x / divisor.x, y / divisor.y); }

    double magnitude() { return std::hypot(x, y); }
    // More efficient way of getting the squared magitude if that is usable
	constexpr double getMagnitudeSquared() { return x * x + y * y; }
    Point2D normalize() { double mag = magnitude(); return Point2D(x / mag, y / mag); }
	constexpr double dot(Point2D other) const { return x * other.x + y * other.y; }
    // Inverse arctan function for determining the angle away from the x-axis
    double X_Axis_atan2() const { return std::atan2(y, x); }
    // Inverse arctan function for determining the angle away from the y-axis
    double Y_Axis_atan2() const { return std::atan2(x, y); }
    // Returns true if either component is not-a-number, and false otherwise
    bool hasNan() { return std::isnan(x) || std::isnan(y); }
	constexpr bool isZero() { return (x == 0 && y == 0); }
	double distance(Point2D other) const;
	constexpr double det(Point2D const& otherPoint) const {return x * otherPoint.y - otherPoint.x * y; }

    // linearly interpolates between two points, with 0 being at this point and 1 being at the other point
	constexpr Point2D lerp(Point2D const & otherPoint, double factor) const {
		double outX = (1 - factor) * x + factor * otherPoint.x;
		double outY = (1 - factor) * y + factor * otherPoint.y;
		return Point2D(outX, outY);
	}

	std::string stringify() const;
    double x;
    double y;
};

inline Logger& operator<<(Logger& loggerToUse, const Point2D& obj) {
	loggerToUse << obj.stringify();
	return loggerToUse;
}

inline constexpr bool operator==(const Point2D& lhs, const Point2D& rhs) {
	return (lhs.x == rhs.x && lhs.y == rhs.y);
}

inline constexpr void operator+=(Point2D& lhs, const Point2D& rhs) {
	lhs.x += rhs.x;
	lhs.y += rhs.y;
}

inline constexpr Point2D operator+(Point2D lhs, const Point2D& rhs) {
	lhs += rhs;
	return lhs;
}

inline constexpr void operator-=(Point2D& lhs, const Point2D& rhs) {
	lhs.x -= rhs.x;
	lhs.y -= rhs.y;
}

inline constexpr Point2D operator-(Point2D lhs, const Point2D& rhs) {
	lhs -= rhs;
	return lhs;
}

inline constexpr void operator*=(Point2D& lhs, const double& rhs) {
	lhs.x *= rhs;
	lhs.y *= rhs;
}

inline constexpr Point2D operator*(Point2D lhs, const double& rhs) {
	lhs *= rhs;
	return lhs;
}

inline constexpr Point2D operator*(const double& lhs, Point2D rhs) {
	return rhs * lhs;
}

inline constexpr void operator/=(Point2D& lhs, const double& rhs) {
	lhs.x /= rhs;
	lhs.y /= rhs;
}

inline constexpr Point2D operator/(Point2D lhs, const double& rhs) {
	lhs /= rhs;
	return lhs;
}

inline constexpr Point2Di clamp(Point2Di num, Point2Di min, Point2Di max) {
	int x = std::clamp(num.x, min.x, max.x);
	int y = std::clamp(num.y, min.y, max.y);
	return Point2Di(x, y);
}

inline constexpr Point2Df clamp(Point2Df num, Point2Df min, Point2Df max) {
	float x = std::clamp(num.x, min.x, max.x);
	float y = std::clamp(num.y, min.y, max.y);
	return Point2Df(x, y);
}

inline constexpr Point2D clamp(Point2D num, Point2D min, Point2D max) {
	double x = std::clamp(num.x, min.x, max.x);
	double y = std::clamp(num.y, min.y, max.y);
	return Point2D(x, y);
}

inline constexpr Point3Di clamp(Point3Di num, Point3Di min, Point3Di max) {
	int x = std::clamp(num.x, min.x, max.x);
	int y = std::clamp(num.y, min.y, max.y);
	int z = std::clamp(num.z, min.z, max.z);
	return Point3Di(x, y, z);
}

inline constexpr Point3Df clamp(Point3Df num, Point3Df min, Point3Df max) {
	float x = std::clamp(num.x, min.x, max.x);
	float y = std::clamp(num.y, min.y, max.y);
	float z = std::clamp(num.z, min.z, max.z);
	return Point3Df(x, y, z);
}

inline constexpr Point3D clamp(Point3D num, Point3D min, Point3D max) {
	double x = std::clamp(num.x, min.x, max.x);
	double y = std::clamp(num.y, min.y, max.y);
	double z = std::clamp(num.z, min.z, max.z);
	return Point3D(x, y, z);
}

template<typename T>
double distanceBetween(T point1, T point2) {
	return std::sqrt(squaredDistanceBetween(point1, point2));
}

template<typename T>
double squaredDistanceBetween(T point1, T point2) {
	return (myPow(point1.x - point2.x, 2) + myPow(point1.y - point2.y, 2) + myPow(point1.z - point2.z, 2));
}

// Assumes axisDirection is normalised
template<typename T>
constexpr double distanceAlongAxis(T pointToMeasure, T axisOrigin, T axisDirection) {
	return (pointToMeasure - axisOrigin).dot(axisDirection);
}

// Assumes lineDirection is normalised
template<typename T>
constexpr T nearestPointOnLine(T pointToMeasure, T lineOrigin, T lineDirection) {
	return lineOrigin + distanceAlongAxis(pointToMeasure, lineOrigin, lineDirection) * lineDirection;
}

// Assumes lineDirection is normalised
template<typename T>
constexpr T shortestVectorToLine(T pointToMeasure, T lineOrigin, T lineDirection) {
	return pointToMeasure - nearestPointOnLine(pointToMeasure, lineOrigin, lineDirection);
}