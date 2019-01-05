#include "ResultsDownloader.h"

#include "Debug.h"

#include <stdio.h>
#include <curl/curl.h>
#include <tchar.h>
#include "unzip.h"

#pragma warning(disable : 4996)

const std::string TempZipFileName = "downloads/TempResults.zip";
const std::wstring LTempZipFileName(TempZipFileName.begin(), TempZipFileName.end());

struct FtpFile {
	const char *filename;
	FILE *stream;
};

static size_t my_fwrite(void *buffer, size_t size, size_t nmemb, void *stream)
{
	struct FtpFile *out = (struct FtpFile *)stream;
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

void ResultsDownloader::loadZippedFile(std::string url, std::string newFileName, std::string match)
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

	// Now extract the xml file

	std::wstring matchL(match.begin(), match.end());
	HZIP hz = OpenZip(_T(TempZipFileName.c_str()), 0);
	// -1 gives overall information about the zipfile
	ZIPENTRY ze; GetZipItem(hz, -1, &ze); int numitems = ze.index;
	for (int zi = 0; zi<numitems; zi++)
	{
		GetZipItem(hz, zi, &ze);
		std::wstring zippedName = ze.name;
		if (!matchL.size() || zippedName.find(matchL)) {
			if (zippedName.find(L".xml") != std::wstring::npos) {
				std::wstring s(newFileName.begin(), newFileName.end());
				UnzipItem(hz, zi, s.c_str());
			}
		}
	}
	CloseZip(hz);

	return;
}