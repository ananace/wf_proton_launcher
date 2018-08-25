#include <string>
#include <vector>
#include <unordered_map>

class Hashlist
{
public:
	struct RemoteHash
	{
		std::string SourceFile;
		std::string TargetFile;
		std::string Hash;
	};

    Hashlist();
    Hashlist(const std::string& data);
	Hashlist(const Hashlist& copy) = default;
	Hashlist(Hashlist&& move) = default;
    ~Hashlist();

	Hashlist& operator=(const Hashlist& copy) = default;
	Hashlist& operator=(Hashlist&& move) = default;

    void parseLocal(const std::string& data);
	void hashLocal();

    std::vector<std::string> getDiffering() const;
	const RemoteHash& getRemoteHash(const std::string& it) const;

private:
    std::unordered_map<std::string, RemoteHash> mRemoteHashes;
    std::unordered_map<std::string, std::string> mLocalHashes;
};