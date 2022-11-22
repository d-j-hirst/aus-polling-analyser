#pragma once

#include <curl/curl.h>
#include <curl/easy.h>

#define NOMINMAX

#include <string>

class ResultsDownloader {

public:

	ResultsDownloader();

	~ResultsDownloader();

	// loads the file from "url" into a temporary zip file, then unzips any file
	// with name matching "match" into the file named "newFileName"
	void loadZippedFile(std::string url, std::string newFileName, std::string match = "");

	void unzipFile(std::string fileName, std::string newFileName, std::string match = "");

	// loads the file or directory listing from "url" into "outputString"
	void loadUrlToString(std::string url, std::string& outputString);

private:

	std::string zipFileName;
	CURL* curl_handle;

	char errbuf[CURL_ERROR_SIZE];
};