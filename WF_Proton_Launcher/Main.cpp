#include "stdafx.h"
#include "Hashlist.hpp"
#include "MD5.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cassert>

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <curl/curl.h>

#include <lzma/7z.h>
#include <lzma/LzmaDec.h>
#include <lzma/LzmaLib.h>

#ifndef NDEBUG
#define _DEBUG
#endif

const std::string INDEX_HOST = "origin.warframe.com";

const std::string INDEX_PATH = "/index.txt.lzma";

const std::string WARFRAME_EXE = "Warframe.exe";
const std::string WARFRAME_64_EXE = "Warframe.x64.exe";

static const size_t UNDERFLOW_BUFSIZE = 1024 * 16;
static const size_t DECOMPRESS_BUFSIZE = 1024 * 256;

enum CompressedFileStage
{
	Stage_Broken = -1,
	Stage_New = 0,
	Stage_Initialized,
	Stage_Decompressing,
	Stage_Finished,
	Stage_Closed,
};

struct FileWrapper
{
	FILE* stream;
	const Hashlist::RemoteHash& file;
};

struct CompressedFileWrapper
{
	FILE* stream;
	const Hashlist::RemoteHash& file;
	CompressedFileStage stage;

	CLzmaDec dec;
	ELzmaStatus status;
	size_t written, targetSize;

	uint8_t buffer[UNDERFLOW_BUFSIZE];
	size_t bufferSize;
};

static void* _lzmaAlloc(ISzAllocPtr, size_t size) {
	return new uint8_t[size];
}
static void _lzmaFree(ISzAllocPtr, void* addr) {
	if (!addr)
		return;
	delete[] reinterpret_cast<uint8_t*>(addr);
}

static ISzAlloc _allocFuncs = {
	_lzmaAlloc,  _lzmaFree
};

std::string toHumanSize(size_t bytes);

size_t writeMemory(void* contents, size_t size, size_t nmemb, void *userdata);
size_t writeFile(void* contents, size_t size, size_t nmemb, void *userdata);
size_t writeCompressedFile(void* contents, size_t size, size_t nmemb, void *userdata);

void addClient(CURLM *cm, const Hashlist::RemoteHash& file);

void decodeLzmaBuf(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst);
void decodeLzmaFile(FILE* src, FILE* dst);

int launch(const std::string& command);

std::string WARFRAME_LOGO();

bool do_update = true,
     do_cache = false,
     do_launch = true,
     redownload = false,
     firstrun = false,
     verbose = false;

std::string registry_option;

std::string fullscreen_option = " -fullscreen:0 ";

int main(int argc, char** argv)
{
	auto warframe_exe = WARFRAME_64_EXE;

	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "-U")
			do_update = false;
		if (arg == "-c")
			do_cache = true;
		if (arg == "-L")
			do_launch = false;
		if (arg == "-R")
			redownload = true;
		if (arg == "-v")
			verbose = true;
		if (arg == "-32")
			warframe_exe = WARFRAME_EXE;
		if (arg == "-F")
			firstrun = true;
		if (arg.substr(0,10) == "-registry:")
                        registry_option = arg;
		if (arg == "-fullscreen")
			fullscreen_option = " -fullscreen:1 ";
		if (arg == "-h")
		{
			std::cout << "Usage: " << argv[0] << " [-F -U -c -L -R -v -32 -h]" << std::endl << std::endl
				<< "Parameters;" << std::endl
				<< "  -F  Set up first run Wine requirements" << std::endl
				<< "  -U  Skip updating" << std::endl
				<< "  -c  Optimize cache before launch" << std::endl
				<< "  -L  Skip launching the game" << std::endl
				<< "  -R  Redownload the entire game" << std::endl
				<< "  -v  Print more verbose output" << std::endl
				<< "  -32 Launch the 32-bit binary instead of the 64-bit one" << std::endl
				<< "  -fullscreen Launching the game in full-screen" << std::endl
				<< "  -h  Get this text" << std::endl;

			return 0;
		}
	}

	std::cout << WARFRAME_LOGO() << std::endl;
	std::cout << "Replacement Warframe Launcher by Ananace" << std::endl;
	std::cout << "https://github.com/ananace/wf_proton_launcher" << std::endl << std::endl;

	curl_global_init(CURL_GLOBAL_ALL);

	Hashlist hashes;
	std::vector<std::string> diff;

	if (firstrun) {
		std::cout << std::endl << "Installing Direct X...";

		FILE* fp;
		fopen_s(&fp, "directx_Jun2010_redist.exe", "wb");
		if (!fp)
		{
			std::wcout << "Failed to create file on disk." << std::endl;
			return 1;
		}
		CURL *curl = curl_easy_init();

		FileWrapper wrap{ fp, {} };
		curl_easy_setopt(curl, CURLOPT_URL, "https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe");
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Warframe_on_Proton)");
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wrap);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFile);

		CURLcode res;
		res = curl_easy_perform(curl);

		if (res != CURLE_OK) {
			std::wcout << "Failed to download Direct X." << std::endl;
			return 1;
		}

		curl_easy_cleanup(curl);
		fclose(fp);

		launch("directx_Jun2010_redist.exe /Q /T:C:\\dx9temp");
		launch("C:\\dx9temp/DXSETUP.exe /silent");
		std::wcout << " Done." << std::endl;

		std::cout << "Adding XAudio2_7 registry override." << std::endl;
		launch("REG ADD HKCU\\Software\\Wine\\DllOverrides /v xaudio2_7 /t REG_SZ /d native /f");

		fs::remove_all("C:\\dx9temp");
		fs::remove("directx_Jun2010_redist.exe");
		std::wcout << " Done." << std::endl;

	}

	if (do_update)
	{
		std::cout << "Retrieving index... ";

		std::string data;
		{
			std::vector<uint8_t> index_data;

			CURL *curl;
			CURLcode res;

			curl = curl_easy_init();
			if (!curl)
				return 1;

			curl_easy_setopt(curl, CURLOPT_URL, ("http://" + INDEX_HOST + INDEX_PATH).c_str());
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Warframe_on_Proton)");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeMemory);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&index_data);

			res = curl_easy_perform(curl);

			if (res != CURLE_OK)
			{
				std::cout << "!!Failed!!" << std::endl << "Server might be having troubles, check again in a few minutes." << std::endl;
				return 1;
			}

			std::cout << "Done." << std::endl;

			std::vector<uint8_t> decoded_index;

			decodeLzmaBuf(index_data, decoded_index);
			index_data.clear();

			data = std::string(reinterpret_cast<char*>(&decoded_index[0]), decoded_index.size());
		}

		std::string oldmd5, newmd5;
		
		newmd5 = MD5(data).hexdigest();

		hashes = std::move(Hashlist(data));

		std::cout << "Verifying local game state... ";

		if (!redownload)
		{
			if (fs::is_regular_file({ "./local_index.txt" }))
			{
				FILE* fp;
				fopen_s(&fp, "./local_index.txt", "rb");
				fseek(fp, 0, SEEK_END);
				size_t size = ftell(fp);
				rewind(fp);

				std::string localData;
				localData.resize(size);
				fread_s(&localData[0], size, 1, size, fp);
				fclose(fp);

				hashes.parseLocal(localData);
			}

			hashes.hashLocal();
		}

		FILE* fp;
		fopen_s(&fp, "./local_index.txt", "wb");
		fwrite(data.data(), 1, data.size() + 1, fp);
		fclose(fp);

		data.clear();

		diff = hashes.getDiffering();
	}

    if (diff.empty())
    {
		std::cout << "Done, no updates found." << std::endl;
    }
	else
    {
		std::cout << "Done" << std::endl << diff.size() << " files need to be updated." << std::endl;

		CURLM *cm = curl_multi_init();
		CURLMsg *msg;

		curl_multi_setopt(cm, CURLMOPT_MAXCONNECTS, 4L);

		fd_set read, write, error;

		auto downloadit = diff.cbegin();
		for (int i = 0; i < 4; ++i)
		{
			addClient(cm, hashes.getRemoteHash(*downloadit++));
			if (downloadit == diff.cend())
				break;
		}

		bool failed = false;

		int running = -1;
		curl_multi_perform(cm, &running);

		do
		{
			FD_ZERO(&read);
			FD_ZERO(&write);
			FD_ZERO(&error);

			int queue = -1;
			int max = -1;
			long timeout;
			timeval timeout_val;

			curl_multi_timeout(cm, &timeout);
			if (timeout > 0)
			{
				timeout_val.tv_sec = timeout / 1000;
				timeout_val.tv_usec = (timeout % 1000) * 1000;
			}

			CURLMcode mc;
			if ((mc = curl_multi_fdset(cm, &read, &write, &error, &max)) != CURLM_OK)
			{
				std::cerr << "Error: curl_multi_fdset" << std::endl;
				return 1;
			}

			int rc;
			if (max == -1)
			{
				Sleep(100);
				rc = 0;
			}
			else
				rc = select(max + 1, &read, &write, &error, &timeout_val);

			switch (rc) {
			case -1:
				break;

			case 0:
			default:
				curl_multi_perform(cm, &running);
				break;
			}

			while ((msg = curl_multi_info_read(cm, &queue)))
			{
				if (msg->msg == CURLMSG_DONE)
				{
					CompressedFileWrapper *file;
					CURL* curl = msg->easy_handle;
					curl_easy_getinfo(curl, CURLINFO_PRIVATE, &file);
					CURLcode code = msg->data.result;
					std::string compression_result = file->stage == Stage_Broken ? "Decompression failed!" : "Decompressed successfully.";
					std::cout << "Downloaded " << toHumanSize(file->targetSize) << " " << file->file.TargetFile << ": " << curl_easy_strerror(code) << ", " << compression_result << std::endl;

					curl_multi_remove_handle(cm, curl);
					curl_easy_cleanup(curl);

					std::string filename = file->file.TargetFile;

					fclose(file->stream);
					if (code == CURLE_OK && file->stage != Stage_Broken)
					{
						/*
						workQueue.dispatch([filename] {
							FILE* source;
							FILE* target;
							fopen_s(&source, fs::path({ ".." + filename + ".lzma" }).generic_string().c_str(), "rb");
							fopen_s(&target, fs::path({ ".." + filename }).generic_string().c_str(), "wb");

							std::cout << "Decompressing " << filename << "..." << std::endl;
							decodeLzmaFile(source, target);

							fclose(target);
							fclose(source);

							fs::remove({ (".." + filename + ".lzma") });
						});
						*/
					}
					else
					{
						failed = true;
						fs::remove({ (".." + filename) });
					}

					delete file;
				}
				else
				{
					std::cerr << "Error: CURLMsg (" << msg->msg << ")" << std::endl;
					failed = true;
				}

				if (downloadit != diff.cend())
				{
					addClient(cm, hashes.getRemoteHash(*downloadit++));
					running++;
				}
			}
		} while (running);

		curl_multi_cleanup(cm);
		curl_global_cleanup();

        if (failed)
        {
			std::cout << "Some updated files failed to be retrieved";
			if (do_launch)
				std::cout << ", aborting launch.";
			std::cout << std::endl;

            return 1;
        }
        else
        {
            std::cout << "Updated files retrieved, running internal patcher...";
            launch("..\\" + warframe_exe + " -silent -log:/Preprocessing.log -dx10:1 -dx11:1 -threadedworker:1 -cluster:public -language:en -applet:/EE/Types/Framework/ContentUpdate " + registry_option);
            std::cout << " Done." << std::endl;
        }
    }

	if (do_cache)
	{
		std::cout << std::endl << "Optimizing Cache...";
		launch("..\\" + warframe_exe + " -silent -log:/Preprocessing.log -dx10:1 -dx11:1 -threadedworker:1 -cluster:public -language:en -applet:/EE/Types/Framework/CacheDefraggerAsync /Tools/CachePlan.txt " + registry_option);
		std::wcout << " Done." << std::endl;
	}

	if (do_launch)
		launch("..\\" + warframe_exe + " -silent -log:/Preprocessing.log -dx10:1 -dx11:1 -threadedworker:1 -cluster:public -language:en" + fullscreen_option + registry_option);

    return 0;
}

std::string toHumanSize(size_t bytes)
{
	if (bytes < 1024)
		return std::to_string(bytes) + "B";
	else if (bytes < 1024 * 1024)
		return std::to_string(bytes / 1024) + "KB";
	else if (bytes < 1024 * 1024 * 1024)
		return std::to_string(bytes / 1024 / 1024) + "MB";
	return std::to_string(bytes / 1024 / 1024 / 1024) + "GB";
}

size_t writeMemory(void* contents, size_t dlSize, size_t nmemb, void *userdata)
{
	size_t realSize = dlSize * nmemb;
	std::vector<uint8_t> &data = *reinterpret_cast<std::vector<uint8_t>*>(userdata);

	size_t size = data.size();
	data.resize(size + realSize);
	memcpy(&data[size], contents, realSize);
	size += realSize;

	return realSize;
}
size_t writeFile(void* contents, size_t size, size_t nmemb, void *userdata)
{
	FileWrapper &wrapper = *reinterpret_cast<FileWrapper*>(userdata);
	return fwrite(contents, size, nmemb, wrapper.stream);
}
size_t writeCompressedFile(void* contents, size_t size, size_t nmemb, void *userdata)
{
	CompressedFileWrapper &wrapper = *reinterpret_cast<CompressedFileWrapper*>(userdata);
	uint8_t* data = reinterpret_cast<uint8_t*>(contents);
	size_t realSize = size * nmemb;

	if (wrapper.stage == Stage_New)
	{
		uint8_t props[LZMA_PROPS_SIZE];
		memcpy(props, data, LZMA_PROPS_SIZE);
		data += LZMA_PROPS_SIZE;
		realSize -= LZMA_PROPS_SIZE;

		uint64_t dstSize;
		memcpy(&dstSize, data, sizeof(uint64_t));
		data += sizeof(uint64_t);
		realSize -= sizeof(uint64_t);

		LzmaDec_Construct(&wrapper.dec);
		SRes res = LzmaDec_Allocate(&wrapper.dec, props, LZMA_PROPS_SIZE, &_allocFuncs);
		LzmaDec_Init(&wrapper.dec);

		wrapper.written = wrapper.targetSize = wrapper.bufferSize = 0;
		wrapper.targetSize = dstSize;

		wrapper.stage = Stage_Decompressing;
	}

	if (wrapper.stage == Stage_Decompressing)
	{
		size_t realInputSize = realSize,
			   realOutputSize = DECOMPRESS_BUFSIZE;

		uint8_t outputBuf[DECOMPRESS_BUFSIZE];

		size_t inputSize = realInputSize,
			   outputSize = realOutputSize;

		size_t readBufOffset = 0;
		while (readBufOffset < realInputSize)
		{
			SRes res = LzmaDec_DecodeToBuf(&wrapper.dec,
				&outputBuf[0], &outputSize,
				data + readBufOffset, &inputSize,
				LZMA_FINISH_ANY,
				&wrapper.status);

			if (res != SZ_OK)
				wrapper.stage = Stage_Broken;

			if (outputSize > 0)
			{
				fwrite(&outputBuf[0], 1, outputSize, wrapper.stream);
				wrapper.written += outputSize;
			}

			readBufOffset += inputSize;
			inputSize = realInputSize - readBufOffset;

			if (wrapper.status == LZMA_STATUS_FINISHED_WITH_MARK || wrapper.written >= wrapper.targetSize)
				wrapper.stage = Stage_Finished;
		}
	}

	if (wrapper.stage == Stage_Finished)
	{
		LzmaDec_Free(&wrapper.dec, &_allocFuncs);

		wrapper.stage = Stage_Closed;
	}

	return size * nmemb;
}

void decodeLzmaBuf(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst)
{
	if (src.size() < 13)
		return;

	size_t readPtr = 0;

	uint8_t props[LZMA_PROPS_SIZE];
	memcpy(props, &src[readPtr], LZMA_PROPS_SIZE);
	readPtr += LZMA_PROPS_SIZE;

	uint64_t dstSize;
	memcpy(&dstSize, &src[readPtr], sizeof(uint64_t));
	readPtr += sizeof(uint64_t);

	uint64_t inSize = src.size() - readPtr,
		     outSize = min(dstSize, inSize * 4);

	dst.resize(outSize);

	SRes ret = LzmaUncompress(
		&dst[0], &outSize,
		&src[readPtr], &inSize,
		props, LZMA_PROPS_SIZE);

	if (ret != SZ_OK)
		return;

	if (dst.size() != outSize)
		dst.resize(outSize);
}

void decodeLzmaFile(FILE* src, FILE* dst)
{
	if (!src || !dst)
		return;

	uint8_t props[LZMA_PROPS_SIZE];
	fread_s(props, LZMA_PROPS_SIZE, 1, LZMA_PROPS_SIZE, src);

	uint64_t dstSize;
	fread_s(&dstSize, sizeof(uint64_t), 1, sizeof(uint64_t), src);

	fseek(src, 0, SEEK_END);
	uint64_t srcSize = ftell(src) - LZMA_PROPS_SIZE - 8;

	fseek(src, 0, SEEK_SET);
	fseek(src, LZMA_PROPS_SIZE, SEEK_CUR);
	fseek(src, 8, SEEK_CUR);


	CLzmaDec dec;
	LzmaDec_Construct(&dec);
	SRes res = LzmaDec_Allocate(&dec, props, LZMA_PROPS_SIZE, &_allocFuncs);
	LzmaDec_Init(&dec);


	size_t written = 0, read = 0;

	uint8_t inputBuf[DECOMPRESS_BUFSIZE / 6];
	uint8_t outputBuf[DECOMPRESS_BUFSIZE];
	ELzmaStatus status;

	while (written < dstSize)
	{
		size_t inBufContentLen = fread_s(inputBuf, DECOMPRESS_BUFSIZE / 6, 1, DECOMPRESS_BUFSIZE / 6, src);
		read += inBufContentLen;

		size_t inputSize = inBufContentLen;
		size_t outputSize = min(DECOMPRESS_BUFSIZE, dstSize);

		res = LzmaDec_DecodeToBuf(&dec,
			outputBuf, &outputSize,
			inputBuf, &inputSize,
			LZMA_FINISH_ANY,
			&status);

		if (outputSize > 0)
		{
			fwrite(outputBuf, 1, outputSize, dst);
			written += outputSize;
		}

		if (outputSize == 0 && inputSize == 0)
			break;

		if (status == LZMA_STATUS_FINISHED_WITH_MARK)
			break;
	}

	LzmaDec_Free(&dec, &_allocFuncs);
}

void addClient(CURLM *cm, const Hashlist::RemoteHash& file)
{
	std::string url = "http://" + INDEX_HOST + file.SourceFile;
	fs::path target = ".." + file.TargetFile;
	fs::create_directories(target.parent_path());

	FILE* stream;
	fopen_s(&stream, target.generic_string().c_str(), "wb");

	CompressedFileWrapper* wrapper = new CompressedFileWrapper{
		stream,
		file,
		Stage_New
	};

	CURL* curl = curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Warframe_on_Proton)");
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_PRIVATE, (void*)wrapper);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCompressedFile);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)wrapper);
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L * 64L);
	curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

	curl_multi_add_handle(cm, curl);
}

class FSRename
{
public:
	FSRename(const fs::path& orig, const fs::path& changed)
		: mOrigPath(orig)
		, mChangedPath(changed)
		, mRenamed(false)
	{
		if (fs::is_regular_file(orig))
		{
			rename(orig.generic_string().c_str(), changed.generic_string().c_str());
			mRenamed = true;
		}
	}
	~FSRename()
	{
		if (mRenamed)
		{
			if (fs::is_regular_file(mOrigPath))
				fs::remove(mOrigPath);

			rename(mChangedPath.generic_string().c_str(), mOrigPath.generic_string().c_str());
		}
	}

private:
	fs::path mOrigPath, mChangedPath;
	bool mRenamed;
};

int launch(const std::string& command)
{
	if (verbose)
		std::cout << "> " << command << std::endl;

	FSRename launcher_keeper("Launcher.exe", "Launcher.exe.custom");
	return system((command).c_str());
}

std::string WARFRAME_LOGO()
{
	const std::vector<uint8_t> DATA = {
		0x5d, 0x00, 0x00, 0x80, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x10, 0x6e,
		0x16, 0x2c, 0x05, 0x58, 0x71, 0x50, 0x46, 0xeb, 0x4b, 0xbc, 0x66, 0x4d, 0x7a, 0x2e, 0x96, 0xdd,
		0x20, 0x00, 0x48, 0xfb, 0x83, 0xb2, 0xfe, 0xfb, 0x18, 0x23, 0x7b, 0x6e, 0x01, 0x32, 0xbe, 0xef, 
		0x6f, 0x24, 0xf4, 0xd7, 0x88, 0x9d, 0x5b, 0xff, 0xa3, 0x15, 0xf7, 0xc1, 0xdc, 0x90, 0x75, 0x51, 
		0x5c, 0x30, 0xcf, 0xbb, 0x64, 0x3e, 0x79, 0xc6, 0x9f, 0x8b, 0xfd, 0xcf, 0x7e, 0x05, 0xcf, 0x3d, 
		0xdf, 0xc9, 0x9c, 0xb5, 0x18, 0x29, 0xe7, 0xfc, 0xe9, 0x8c, 0xf9, 0x65, 0x7a, 0xf6, 0x2a, 0xb7, 
		0x43, 0xc2, 0xa7, 0xe4, 0xf8, 0xe8, 0x6f, 0x42, 0x55, 0xe0, 0xb2, 0xec, 0x9d, 0x4e, 0xf8, 0x2f, 
		0x9f, 0x28, 0x84, 0x91, 0x12, 0x75, 0x04, 0xa4, 0x8d, 0x70, 0xc5, 0x1f, 0x8d, 0x4e, 0x55, 0x55, 
		0x9d, 0x61, 0x5c, 0x46, 0xa0, 0xdd, 0x84, 0x65, 0xdb, 0x32, 0x4d, 0xe9, 0xe4, 0x9d, 0xfb, 0xdb, 
		0x05, 0xd1, 0x57, 0x03, 0x8b, 0x6c, 0x93, 0x57, 0xc8, 0xfa, 0x6c, 0x38, 0x65, 0x93, 0xe9, 0xc2, 
		0xb1, 0xd3, 0xc4, 0xf6, 0xff, 0x34, 0x3f, 0xac, 0x92, 0x48, 0x5a, 0xfa, 0xd7, 0x10, 0x69, 0xe9, 
		0xe7, 0xa3, 0xe2, 0x03, 0xff, 0xb9, 0x55, 0xce, 0xd6, 0x5e, 0x46, 0x1e, 0xab, 0x0e, 0x5d, 0x32, 
		0x98, 0xe1, 0x69, 0xef, 0x98, 0x4c, 0x69, 0x74, 0xc8, 0xe7, 0x0e, 0x08, 0x80, 0xae, 0x3c, 0xc4, 
		0x39, 0x8e, 0x93, 0xb7, 0x41, 0x40, 0x76, 0xe9, 0xa7, 0xe1, 0x47, 0xe2, 0xaa, 0xe5, 0x63, 0x3a, 
		0xaa, 0x2c, 0xde, 0x8e, 0x7d, 0x2a, 0x3d, 0xe9, 0x03, 0x2e, 0x32, 0xdb, 0xa1, 0x01, 0x1f, 0xee, 
		0x5b, 0x3f, 0x91, 0x00, 0xf5, 0xe4, 0x6c, 0x4b, 0xa3, 0x47, 0x6c, 0x8d, 0x49, 0xc1, 0xdf, 0x26, 
		0x37, 0xe0, 0xda, 0x50, 0x50, 0x6a, 0xe6, 0x43, 0x2d, 0xb6, 0xad, 0x5f, 0x30, 0x91, 0x0a, 0x80, 
		0xf6, 0xbb, 0x71, 0xe9, 0x4d, 0x85, 0x57, 0x85, 0x0e, 0x67, 0x3c, 0x3a, 0x9b, 0x81, 0xe2, 0xb5, 
		0x08, 0x15, 0xe3, 0xf4, 0x6e, 0x04, 0x7d, 0x20, 0x3a, 0x38, 0xcc, 0xfc, 0x51, 0x8d, 0xe9, 0x7d, 
		0x64, 0x14, 0x38, 0xbe, 0xd3, 0xfa, 0x0d, 0xb3, 0xd1, 0xe7, 0x46, 0x49, 0x23, 0x8a, 0x79, 0x9b, 
		0x3a, 0x79, 0xa2, 0xc1, 0xc4, 0xfe, 0x0c, 0xb7, 0x42, 0x0f, 0xda, 0x46, 0x64, 0x72, 0x37, 0x7c, 
		0xed, 0x29, 0x11, 0xab, 0xc6, 0x32, 0x08, 0xfb, 0xcc, 0x94, 0x47, 0xb8, 0x53, 0x26, 0xef, 0x43, 
		0xf3, 0x8b, 0x00, 0x43, 0xb7, 0x15, 0x13, 0xf0, 0xb2, 0x2b, 0x8d, 0x40, 0x7f, 0xdd, 0x85, 0x77, 
		0xb9, 0x17, 0x9f, 0x32, 0x40, 0xbd, 0x4e, 0x07, 0x37, 0xbc, 0xfe, 0x8d, 0x1f, 0x5e, 0xb9, 0x40, 
		0xa0, 0xfa, 0x05, 0x1c, 0x11, 0xae, 0x4b, 0x0c, 0xe0, 0x93, 0x23, 0x94, 0xea, 0x15, 0x77, 0x5a, 
		0x8d, 0x98, 0x21, 0xcf, 0x21, 0xda, 0xd3, 0x66, 0x2a, 0x71, 0x8f, 0xd4, 0x61, 0x9e, 0xb6, 0x78, 
		0x53, 0x3a, 0xa9, 0x0e, 0xd0, 0xde, 0x75, 0x04, 0xfb, 0x92, 0xaf, 0xc5, 0xc8, 0x72, 0xda, 0x8e, 
		0xc8, 0xca, 0xa2, 0x9b, 0x0f, 0x7d, 0x95, 0x1f, 0x98, 0xc2, 0x83, 0xcd, 0x6e, 0x00, 0x08, 0xf3, 
		0x77, 0x93, 0x9a, 0x67, 0x68, 0xd9, 0x59, 0x4e, 0x2f, 0x30, 0xd7, 0xd3, 0x2a, 0xb7, 0xa0, 0x06, 
		0xc2, 0x74, 0x41, 0xe7, 0x2c, 0x6c, 0x63, 0xd0, 0xc5, 0x30, 0x2e, 0xe0, 0xab, 0x02, 0x3d, 0x53, 
		0x7f, 0x9b, 0x76, 0x72, 0x1a, 0x4a, 0xe5, 0xcc, 0x36, 0x85, 0xdb, 0x9e, 0x7c, 0xda, 0x6c, 0xef, 
		0x2f, 0x4c, 0xac, 0xdb, 0x63, 0x15, 0x16, 0x94, 0x97, 0xa4, 0x0c, 0x6a, 0xb9, 0x98, 0xc6, 0x09, 
		0x9b, 0x26, 0x27, 0x50, 0x0a, 0x20, 0x46, 0x95, 0xbc, 0xb7, 0xfa, 0xfa, 0x7d, 0xaa, 0xbc, 0x57, 
		0xa5, 0x31, 0x32, 0x3d, 0xc0, 0x46, 0x9f, 0x93, 0x5d, 0x95, 0x10, 0x13, 0xa2, 0x2c, 0x7f, 0xc7, 
		0x24, 0xbb, 0xe3, 0x3a, 0x4a, 0xa4, 0xc3, 0x12, 0x22, 0xcc, 0x07, 0x38, 0x9d, 0xbb, 0x0f, 0x58, 
		0x9a, 0xd1, 0x99, 0x65, 0x3b, 0x62, 0xe5, 0x55, 0xea, 0xec, 0x9a, 0xd5, 0x83, 0x50, 0x59, 0x67, 
		0x55, 0xfc, 0x6f, 0xef, 0xf3
	};
	std::vector<uint8_t> decoded;

	decodeLzmaBuf(DATA, decoded);

	return std::string(reinterpret_cast<char*>(&decoded[0]));
}
