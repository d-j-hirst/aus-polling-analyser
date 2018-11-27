#pragma once

#include <curl/curl.h>
#include <curl/easy.h>

#define NOMINMAX

#include <string>

const std::string TempResultsXmlFileName = "downloads/results.xml";
const std::wstring LTempResultsXmlFileName(TempResultsXmlFileName.begin(), TempResultsXmlFileName.end());

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