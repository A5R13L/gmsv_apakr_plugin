#include <Bootil/Bootil.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <bitset>
#include <algorithm>
#include <numeric>

// Key is the current encryption key (64 characters of hex)

inline const char *APAKR_DECRYPTION_FUNCTION = R"(
	local KeyLength = #Key

	// https://github.com/philanc/plc/blob/master/plc/rc4.lua

	local function RC4(key, plain)
		local function step(s, i, j)
			i = bit.band(i + 1, 0xff)
			local ii = i + 1
			j = bit.band(j + s[ii], 0xff)
			local jj = j + 1
			s[ii], s[jj] = s[jj], s[ii]
			local k = s[bit.band(s[ii] + s[jj], 0xff) + 1]

			return s, i, j, k
		end

		local function keysched(key)
			local s = {}
			local j, ii, jj = 0

			for i = 0, 255 do
				s[i + 1] = i
			end

			for i = 0, 255 do
				ii = i + 1
				j = bit.band(j + s[ii] + string.byte(key, (i % 32) + 1), 0xff)
				jj = j + 1
				s[ii], s[jj] = s[jj], s[ii]
			end

			return s
		end

		local s = keysched(key)
		local i, j = 0, 0
		local k
		local Output = {}

		for n = 1, #plain do
			s, i, j, k = step(s, i, j)
			Output[n] = string.char(bit.bxor(string.byte(plain, n), k))
		end

		return table.concat(Output)
	end

	local function APakr_Decrypt(PackContents)
		local KeyBytes = {}

		for Index = 1, #Key, 2 do
			KeyBytes[#KeyBytes + 1] = string.char(tonumber(Key:sub(Index, Index + 1), 16))
		end

		return RC4(table.concat(KeyBytes), PackContents)
	end
)";

inline std::vector<uint8_t> HexStringToBytes(const std::string &hex)
{
	std::vector<uint8_t> bytes;

	for (size_t i = 0; i < hex.size(); i += 2)
	{
		std::string byteString = hex.substr(i, 2);
		uint8_t _byte = std::stoi(byteString, nullptr, 16);

		bytes.push_back(_byte);
	}

	return bytes;
}

inline std::string RC4(const std::vector<uint8_t> &key, char *data, int size)
{
	size_t keyLength = key.size();

	std::vector<uint8_t> s(256);

	std::iota(s.begin(), s.end(), 0);

	int j = 0;
	for (int i = 0; i < 256; ++i)
	{
		j = (j + s[i] + key[i % keyLength]) % 256;

		std::swap(s[i], s[j]);
	}

	int i = 0;

	j = 0;

	std::string output;

	output.reserve(size);

	for (size_t index = 0; index < (size_t)size; ++index)
	{
		i = (i + 1) % 256;
		j = (j + s[i]) % 256;

		std::swap(s[i], s[j]);

		int k = s[(s[i] + s[j]) % 256];
		uint8_t _byte = data[index] ^ k;

		output.push_back(static_cast<char>(_byte));
	}

	return output;
}

inline void Apakr_Encrypt(Bootil::AutoBuffer &DataPack, Bootil::AutoBuffer &EncryptedDataPack,
						  std::string CurrentPackKey)
{
	std::vector<uint8_t> KeyBin = HexStringToBytes(CurrentPackKey);
	std::string Data = RC4(KeyBin, (char *)DataPack.GetBase(), DataPack.GetSize());

	EncryptedDataPack.Write(Data.data(), Data.size());
}