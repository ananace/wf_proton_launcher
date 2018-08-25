#include "stdafx.h"
#include "Hashlist.hpp"
#include "DispatchQueue.hpp"
#include "MD5.hpp"

#include <algorithm>
#include <atomic>
#include <thread>

static const int MD5_BLOCKSIZE = 1024 * 1024;

Hashlist::Hashlist()
{
}

Hashlist::Hashlist(const std::string& str)
{
    std::string data = str;
    while (!data.empty())
    {
		auto eol = data.find('\n');
		std::string line;
		if (eol == std::string::npos)
		{
			line = data;
			data.clear();
		}
		else
		{
			line = data.substr(0, eol - 1);
			data.erase(0, eol + 1);
		}

		if (line.empty() || line[0] == 0)
			continue;

		auto comma = line.find(',');
		std::string hash = line.substr(comma - 32 - 5, 32);
		std::string file = line.substr(0, comma - 32 - 5 - 1);
		std::string rawFile = line.substr(0, comma);

		mRemoteHashes[file] = {
			rawFile, file, hash
		};
    }
}

Hashlist::~Hashlist()
{
}

void Hashlist::parseLocal(const std::string& str)
{
	std::string data = str;
	while (!data.empty())
	{
		auto eol = data.find('\n');
		std::string line;
		if (eol == std::string::npos)
		{
			line = data;
			data.clear();
		}
		else
		{
			line = data.substr(0, eol - 1);
			data.erase(0, eol + 1);
		}

		if (line.empty() || line[0] == 0)
			continue;

		auto comma = line.find(',');
		std::string hash = line.substr(comma - 32 - 5, 32);
		std::string file = line.substr(0, comma - 32 - 5 - 1);

		mLocalHashes[file] = hash;
	}
}

void Hashlist::hashLocal()
{
	std::mutex ret_store;

	DispatchQueue workQueue;

	for(auto& kv : mRemoteHashes)
	{
		std::string filename = kv.first;

		if (mLocalHashes.count(filename) > 0)
			continue;

		workQueue.dispatch([filename, &ret_store, this] {
			FILE* fp;
			fopen_s(&fp, (".." + filename).c_str(), "rb");

			if (fp)
			{
				fseek(fp, 0, SEEK_END);
				size_t fSize = ftell(fp);
				rewind(fp);

				MD5 md5;

				size_t blockSize = MD5_BLOCKSIZE;
				if (fSize > 1024 * 1024 * 1024)
					blockSize *= 16;

				std::vector<uint8_t> block(blockSize);
				for (size_t i = 0; i <= fSize;)
				{
					size_t read = fread_s(&block[0], blockSize, 1, blockSize, fp);
					if (read == 0)
						break;

					md5.update(&block[0], read);

					i += read;
				}

				md5.finalize();

				{
					std::lock_guard<std::mutex> lock(ret_store);
					mLocalHashes[filename] = md5.hexdigest();
				}

				fclose(fp);
			}
		});
	}

	int oldcount = 0;
	while (workQueue.busy())
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));

		size_t total = mRemoteHashes.size(),
		       finished = 0;
		{
			std::lock_guard<std::mutex> lock(ret_store);
			finished = mLocalHashes.size();
		}

		for (int i = 0; i < oldcount; ++i)
			printf("\b");
		oldcount = printf("%zi/%zi", finished, total);
	}

	for (int i = 0; i < oldcount; ++i)
		printf("\b");

	workQueue.stop();
	workQueue.join();
}

std::vector<std::string> Hashlist::getDiffering() const
{
    std::vector<std::string> ret;
	
	for (auto it = mRemoteHashes.begin(); it != mRemoteHashes.end(); ++it)
	{
		if (it->second.TargetFile == "/Tools/Launcher.exe")
			continue;

		if ((mLocalHashes.count(it->first) <= 0) || (mLocalHashes.at(it->first) != it->second.Hash))
			ret.push_back(it->first);
	}

    return ret;
}

const Hashlist::RemoteHash& Hashlist::getRemoteHash(const std::string& key) const
{
	return mRemoteHashes.at(key);
}