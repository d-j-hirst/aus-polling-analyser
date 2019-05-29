#include "Points.h"

std::string Point4D::stringify() const
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ", " + std::to_string(w) + ")";
}

Point3D::Point3D(Point3Df basePoint)
	: Point3D(basePoint.x, basePoint.y, basePoint.z)
{}

Point2Df Point3D::getSphericalF() const {
	return Point2Df(float(getLongitude()), float(getLatitude()));
}

Point2D Point3D::getSpherical() const {
	return Point2D(getLongitude(), getLatitude());
}

Point3D Point3D::normalizeMag(double & magnitudeOutput) const {
	magnitudeOutput = magnitude();
	return Point3D(x / magnitudeOutput, y / magnitudeOutput, z / magnitudeOutput);
}

double Point3D::distance(Point3D other) const {
	Point3D thisPoint = *this;
	return (thisPoint - other).magnitude();
}

std::string Point3D::stringify() const
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
}

// Point3Df

Point3Df::Point3Df(Point2Df xy, float z)
	: Point3Df(xy.x, xy.y, z)
{}

Point3Df::Point3Df(Point3D basePoint)
	: Point3Df(float(basePoint.x), float(basePoint.y), float(basePoint.z))
{}

Point2Df Point3Df::getSphericalF() const {
	return Point2Df(getLongitude(), getLatitude());
}

Point2D Point3Df::getSpherical() const {
	return Point2D(double(getLongitude()), double(getLatitude()));
}

std::string Point3Df::stringify() const
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
}


// Point3Di

std::string Point3Di::stringify() const
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
}

// Point2Di

std::string Point2Di::stringify() const
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

double Point2Df::distance(Point2Df other) const {
	Point2Df thisPoint = *this;
	return (thisPoint - other).magnitude();
}

std::string Point2Df::stringify() const
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

Point2Df Point2Df::scale(Point2Df zeroPoint, Point2Df onePoint)
{
	Point2Df scaledPoint;
	scaledPoint.x = (x - zeroPoint.x) / (onePoint.x - zeroPoint.x);
	scaledPoint.y = (y - zeroPoint.y) / (onePoint.y - zeroPoint.y);
	return scaledPoint;
}

double Point2D::distance(Point2D other) const
{
	Point2D thisPoint = *this;
	return (thisPoint - other).magnitude();
}

std::string Point2D::stringify() const
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
}