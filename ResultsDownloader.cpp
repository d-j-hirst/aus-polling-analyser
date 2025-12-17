#include "ResultsDownloader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <stdio.h>
#include <curl/curl.h>
#include <tchar.h>
#include "unzip.h"

#include "Log.h"

#pragma warning(disable : 4996)

const std::string TempZipFileName = "downloads/TempResults.zip";
const std::wstring LTempZipFileName(TempZipFileName.begin(), TempZipFileName.end());
const std::string TempFileName = "downloads/Temp.dat";

struct FtpFile {
	const char* filename;
	FILE* stream;
};

static size_t my_fwrite(void* buffer, size_t size, size_t nmemb, void* stream)
{
	struct FtpFile* out = (struct FtpFile*)stream;
	if (out && !out->stream) {
		/* open file for writing */
		out->stream = fopen(out->filename, "wb");
		if (!out->stream)
			return 0; /* failure, can't open file to write */
	}
	return fwrite(buffer, size, nmemb, out->stream);
}

ResultsDownloader::ResultsDownloader()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);

	curl_handle = curl_easy_init();
}

ResultsDownloader::~ResultsDownloader()
{
	curl_easy_cleanup(curl_handle);

	curl_global_cleanup();
}

std::string ResultsDownloader::loadZippedFile(std::string url, std::string newFileName, bool getPollingDistricts)
{
	CURLcode res;
	struct FtpFile ftpfile = {
		TempZipFileName.c_str(), /* name to store the file as if successful */
		NULL
	};
	if (curl_handle) {
		/*
		* You better replace the URL with one that works!
		*/
		curl_easy_setopt(curl_handle, CURLOPT_URL,
			url.c_str());
		/* Define our callback to get called when there's data to be written */
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, my_fwrite);
		/* Set a pointer to our struct to pass to the callback */
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &ftpfile);

		/* Switch on full protocol/debug output */
		curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

		res = curl_easy_perform(curl_handle);

		if (CURLE_OK != res) {
			/* we failed */
			fprintf(stderr, "curl told us %d\n", res);
		}
	}

	if (ftpfile.stream)
		fclose(ftpfile.stream); /* close the local file */

	return unzipFile(TempZipFileName, newFileName, getPollingDistricts);
}

std::string ResultsDownloader::unzipFile(std::string sourceFileName, std::string newFileName, bool getPollingDistricts)
{
	// Quickest way I could find to achieve this because the previous method started truncating files before the 2024 election
	// and there's probably no good reason to do things more "properly" at this stage
	// If you're not on Windows, replace with whatever code will put the expanded file inside the /downloads folder with filename
	auto command = std::string("powershell.exe -Command \"Expand-Archive ") + sourceFileName + std::string(" -DestinationPath C:\\a\"");
	system(command.c_str());
	std::filesystem::path archivePath("C:/a");
	newFileName = newFileName.substr(0, 10) == "downloads/" ? newFileName : "downloads/" + newFileName;
	std::filesystem::path newPath(newFileName);

	bool transferredFile = false;
	auto transferFile = [&]() {
		for (const auto& entry : std::filesystem::directory_iterator(archivePath)) {
			if (entry.path().string().find(".xml") != std::string::npos) {
				if (entry.path().string().find("eml-") != std::string::npos) continue;
				if (!getPollingDistricts) {
					if (entry.path().string().find("pollingdistricts-") != std::string::npos) continue;
				} else {
					if (entry.path().string().find("preload-") != std::string::npos) continue;
				}
				std::filesystem::rename(entry.path(), newPath);
				transferredFile = true;
			}
		}
	};

	transferFile();
	// This section handles AEC folders which have an "xml" subfolder.
	// (They also have other files with .xml in other subfolders, so we only want to look in this particular subfolder.)
	if (!transferredFile) {
		archivePath = std::filesystem::path("C:/a/xml");
		transferFile();
	}
	if (transferredFile) return newPath.string();
	return "Could not transfer file.";
}

void ResultsDownloader::loadUrlToString(std::string url, std::string& outputString)
{
	CURLcode res;
	struct FtpFile ftpfile = {
		TempFileName.c_str(), /* name to store the file as if successful */
		NULL
	};
	if (curl_handle) {
		/*
		* You better replace the URL with one that works!
		*/
		curl_easy_setopt(curl_handle, CURLOPT_URL,
			url.c_str());
		/* Define our callback to get called when there's data to be written */
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, my_fwrite);
		/* Set a pointer to our struct to pass to the callback */
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &ftpfile);

		/* Switch on full protocol/debug output */
		curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

		res = curl_easy_perform(curl_handle);

		if (CURLE_OK != res) {
			/* we failed */
			fprintf(stderr, "curl told us %d\n", res);
		}
	}

	if (ftpfile.stream)
		fclose(ftpfile.stream); /* close the local file */

								// Now extract the xml file

	std::ifstream tempFile(TempFileName);
	std::stringstream buffer;
	buffer << tempFile.rdbuf();
	outputString = buffer.str();
}