#include <string>
#include <iostream>

extern "C"
{
    bool apakr_filter(const char *Path, const char *Contents)
    {
        std::string _Path(Path);

        if (_Path.find("bad_file_to_pack") != std::string::npos)
            return false;
        else
            return true;
    }

    void apakr_mutate(bool AutoRefresh, const char *Path, const char *Contents, void (*MutatePath)(const char *),
                      void (*MutateContents)(const char *))
    {
        std::string _Path(Path);

        if (!AutoRefresh && _Path.find("file_to_rename") != std::string::npos)
            MutatePath("lua/autorun/client/renamed_file.lua");
        else if (_Path.find("file_to_mutate") != std::string::npos)
            MutateContents("print('This file has been mutated by an APakr Extension')");
    }
}