#pragma once

#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// * SaveFileOutput * //
// Stream enabling writing the data to a save file
// Handles the writing of arbitrary binary data to a file in a
// friendly manner (just use stream operators for plain-old-data
// types and strings).
class SaveFileOutput {
public:
	SaveFileOutput(std::string filename);

	template<typename T, typename std::enable_if_t<std::is_trivial<T>::value>* = nullptr>
	SaveFileOutput& operator<<(T itemToAdd) {
		this->saveStream_.write(reinterpret_cast<char*>(&itemToAdd), sizeof(itemToAdd));
		return *this;
	}

	friend SaveFileOutput& operator<<(SaveFileOutput& stream, std::string itemToAdd);

	template<typename T, typename U>
	inline SaveFileOutput& operator<<(std::unordered_map<T, U> itemToAdd)
	{
		this->outputAsType<int32_t>(itemToAdd.size());
		for (auto const& [t, u] : itemToAdd) {
			*this << t;
			*this << u;
		}
		return *this;
	}

	template<typename T, typename U>
	inline SaveFileOutput& operator<<(std::map<T, U> itemToAdd)
	{
		this->outputAsType<int32_t>(itemToAdd.size());
		for (auto const& [t, u] : itemToAdd) {
			*this << t;
			*this << u;
		}
		return *this;
	}

	template<typename T>
	inline SaveFileOutput& operator<<(std::vector<T> itemToAdd)
	{
		this->outputAsType<int32_t>(itemToAdd.size());
		for (auto const& t : itemToAdd) {
			*this << t;
		}
		return *this;
	}

	template<typename T, int I>
	inline SaveFileOutput& operator<<(std::array<T, I> itemToAdd)
	{
		for (int i = 0; i < I; ++i) {
			*this << itemToAdd[i];
		}
		return *this;
	}

	template<typename T, typename U>
	void outputAsType(U const& output) {
		*this << static_cast<T>(output);
	}

private:
	std::ofstream saveStream_;
};

SaveFileOutput& operator<<(SaveFileOutput& stream, std::string itemToAdd);

// * SaveFileInput * //
// Stream enabling reading the data from a save file
// Handles the reading of arbitrary binary data from a file in a
// friendly manner (just use stream operators for plain-old-data
// types and strings).
class SaveFileInput {
public:
	SaveFileInput(std::string filename);

	bool valid() { return loadStream_.is_open(); }

	template<typename T, typename std::enable_if_t<std::is_trivial<T>::value>* = nullptr>
	SaveFileInput& operator>>(T& itemToRead) {
		this->loadStream_.read(reinterpret_cast<char*>(&itemToRead), sizeof(itemToRead));
		return *this;
	}

	friend SaveFileInput& operator>>(SaveFileInput& stream, std::string& itemToRead);

	template<typename T, typename U>
	inline SaveFileInput& operator>>(std::unordered_map<T, U>& itemToAdd)
	{
		itemToAdd.clear();
		auto count = this->extract<int32_t>();
		for (int i = 0; i < count; ++i) {
			T t; U u;
			*this >> t;
			*this >> u;
			itemToAdd[t] = u;
		}
		return *this;
	}

	template<typename T, typename U>
	inline SaveFileInput& operator>>(std::map<T, U>& itemToAdd)
	{
		itemToAdd.clear();
		auto count = this->extract<int32_t>();
		for (int i = 0; i < count; ++i) {
			T t; U u;
			*this >> t;
			*this >> u;
			itemToAdd[t] = u;
		}
		return *this;
	}

	template<typename T>
	inline SaveFileInput& operator>>(std::vector<T>& itemToAdd)
	{
		itemToAdd.clear();
		auto count = this->extract<int32_t>();
		for (int i = 0; i < count; ++i) {
			itemToAdd.push_back(this->extract<T>());
		}
		return *this;
	}

	template<typename T, int I>
	inline SaveFileInput& operator>>(std::array<T, I>& itemToAdd)
	{
		for (int i = 0; i < I; ++i) {
			*this >> itemToAdd[i];
		}
		return *this;
	}

	template <typename T>
	T extract() {
		T temp;
		*this >> temp;
		return temp;
	}

private:
	std::ifstream loadStream_;
};

SaveFileInput& operator>>(SaveFileInput& stream, std::string& itemToRead);