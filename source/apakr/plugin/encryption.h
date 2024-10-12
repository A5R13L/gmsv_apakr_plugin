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

inline std::vector<uint8_t> HexStringToBytes(const std::string &HexString)
{
    std::vector<uint8_t> Output;

    for (size_t Index = 0; Index < HexString.size(); Index += 2)
    {
        std::string Byte = HexString.substr(Index, 2);

        Output.push_back((uint8_t)std::stoi(Byte, nullptr, 16));
    }

    return Output;
}

inline std::string RC4(const std::vector<uint8_t> &Key, char *Data, int Size)
{
    size_t keyLength = Key.size();

    std::vector<uint8_t> Buffer(256);

    std::iota(Buffer.begin(), Buffer.end(), 0);

    int Value = 0;

    for (int Index = 0; Index < 256; ++Index)
    {
        Value = (Value + Buffer[Index] + Key[Index % keyLength]) % 256;

        std::swap(Buffer[Index], Buffer[Value]);
    }

    int Index = 0;

    Value = 0;

    std::string Output;

    Output.reserve(Size);

    for (int Position = 0; Position < Size; ++Position)
    {
        Index = (Index + 1) % 256;
        Value = (Value + Buffer[Index]) % 256;

        std::swap(Buffer[Index], Buffer[Value]);

        int K = Buffer[(Buffer[Index] + Buffer[Value]) % 256];

        Output.push_back((char)(Data[Position] ^ K));
    }

    return Output;
}

inline void Apakr_Encrypt(Bootil::_AutoBuffer &DataPack, Bootil::_AutoBuffer &EncryptedDataPack,
                          std::string CurrentPackKey)
{
    std::vector<uint8_t> KeyBin = HexStringToBytes(CurrentPackKey);
    std::string Data = RC4(KeyBin, (char *)DataPack.GetBase(), DataPack.GetSize());

    EncryptedDataPack.Write(Data.data(), (uint)Data.size());
}