#pragma once

#include <curl/curl.h>
#include <curl/easy.h>

#define NOMINMAX

#include <string>

class ResultsDownloader {

public:

	ResultsDownloader();

	~ResultsDownloader();

	// loads the file from "url" into a temporary file
	void loadFile(std::string url);

private:

	std::string zipFileName;
	CURL* curl_handle;

	char errbuf[CURL_ERROR_SIZE];
};