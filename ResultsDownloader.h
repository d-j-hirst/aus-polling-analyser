#pragma once

#include <curl/curl.h>
#include <curl/easy.h>

#define NOMINMAX

#include <string>

class ResultsDownloader {

public:

	ResultsDownloader();

	~ResultsDownloader();

	// loads the file from "url" into a temporary zip file
	void loadZippedFile(std::string url, std::string match = "");

private:

	std::string zipFileName;
	CURL* curl_handle;

	char errbuf[CURL_ERROR_SIZE];
};