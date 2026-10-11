#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dataread.hpp"
#include "dataread_port.hpp"
#include "dc2data.hpp"
#include "platform/paths.hpp"

namespace dc = dc2data;
namespace fs = std::filesystem;

namespace {

constexpr std::size_t kFileSize = 3000;
constexpr std::size_t kPadded = 4096;

std::vector<unsigned char> MakeBytes(std::size_t size, unsigned char seed) {
    std::vector<unsigned char> bytes(size);
    for (std::size_t i = 0; i < size; i++) {
        bytes[i] = static_cast<unsigned char>(i * 31 + seed);
    }
    return bytes;
}

void WriteBytes(const fs::path &path, const std::vector<unsigned char> &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// The layout ps2/include/dataread.hpp gives a pack: PACK_ENTRY records ending with an empty name, each
// offset counted from its own record, each next pointer to the record after it.
std::vector<unsigned char> BuildPack(const std::vector<std::pair<std::string, std::vector<unsigned char>>> &files) {
    std::size_t total = (files.size() + 1) * sizeof(PACK_ENTRY);
    std::vector<std::size_t> starts;
    for (const auto &file : files) {
        starts.push_back(total);
        total += (file.second.size() + 3) & ~std::size_t(3);
    }

    std::vector<unsigned char> pack(total, 0);
    for (std::size_t i = 0; i < files.size(); i++) {
        PACK_ENTRY entry{};
        std::snprintf(entry.name, sizeof entry.name, "%s", files[i].first.c_str());
        entry.offset = static_cast<int>(starts[i] - i * sizeof(PACK_ENTRY));
        entry.size = static_cast<int>(files[i].second.size());
        entry.next = static_cast<int>(sizeof(PACK_ENTRY));
        std::memcpy(pack.data() + i * sizeof(PACK_ENTRY), &entry, sizeof entry);
        std::memcpy(pack.data() + starts[i], files[i].second.data(), files[i].second.size());
    }
    return pack;
}

int Load(const char *name, void *buffer, int *size) {
    std::string path(name);
    return LoadFile2(path.data(), buffer, size, LOAD_FILE_READ);
}

class DataRead : public testing::Test {
protected:
    void SetUp() override {
        base_ = fs::temp_directory_path() /
                ("dataread_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        root_ = base_ / "data";
        WriteBytes(root_ / "DIR" / "Sub" / "File.BIN", MakeBytes(kFileSize, 7));
        WriteBytes(root_ / "chara" / "Pack.PAC",
                   BuildPack({{"a.bin", MakeBytes(10, 1)}, {"b.bin", MakeBytes(300, 2)}}));
        WriteBytes(root_ / "small.txt", MakeBytes(10, 3));
        buffer_.assign(kPadded / sizeof(u_long128), 0);
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(base_, error);
    }

    u_long128 *Buffer() { return buffer_.data(); }

    fs::path base_;
    fs::path root_;

private:
    std::vector<u_long128> buffer_;
};

} // namespace

TEST_F(DataRead, IndexLogsItsCountAndRoot) {
    testing::internal::CaptureStderr();
    DataReadIndex(root_);
    std::string log = testing::internal::GetCapturedStderr();
    EXPECT_NE(log.find("dataread: indexed 3 files under " + PathsDisplay(root_)), std::string::npos) << log;
}

TEST_F(DataRead, FindIsCaseInsensitiveAndAcceptsDevicePrefixes) {
    DataReadIndex(root_);
    const fs::path expected = root_ / "DIR" / "Sub" / "File.BIN";
    for (const char *name : {"cdrom0:\\DIR\\SUB\\FILE.BIN;1", "host0:dir/sub/file.bin", "/DIR/Sub/File.BIN",
                             "dir\\sub\\file.bin;1", "cdrom:DIR/SUB/FILE.BIN"}) {
        std::optional<fs::path> found = DataReadFind(name);
        ASSERT_TRUE(found.has_value()) << name;
        EXPECT_TRUE(fs::equivalent(*found, expected)) << name;
    }
}

TEST_F(DataRead, LoadFileReadsExactBytesThenZeroFillsTheSector) {
    DataReadIndex(root_);
    std::memset(Buffer(), 0xAA, kPadded);
    int size = -1;

    ASSERT_EQ(Load("cdrom0:\\DIR\\SUB\\FILE.BIN;1", Buffer(), &size), 1);
    EXPECT_EQ(size, static_cast<int>(kFileSize));

    const auto *bytes = reinterpret_cast<const unsigned char *>(Buffer());
    const std::vector<unsigned char> expected = MakeBytes(kFileSize, 7);
    EXPECT_EQ(std::memcmp(bytes, expected.data(), kFileSize), 0);
    const std::vector<unsigned char> tail(kPadded - kFileSize, 0);
    EXPECT_EQ(std::memcmp(bytes + kFileSize, tail.data(), tail.size()), 0);
}

TEST_F(DataRead, LoadFileAcceptsPlainAndUpperCaseNames) {
    DataReadIndex(root_);
    int size = 0;
    char plain[] = "dir/sub/file.bin";
    char upper[] = "DIR\\SUB\\FILE.BIN;1";
    EXPECT_EQ(LoadFile(plain, Buffer(), &size), 1);
    EXPECT_EQ(size, static_cast<int>(kFileSize));
    EXPECT_EQ(LoadFile(upper, Buffer(), &size), 1);
    EXPECT_EQ(size, static_cast<int>(kFileSize));
}

TEST_F(DataRead, MissingFileReturnsZeroWithoutAborting) {
    DataReadIndex(root_);
    int size = 99;
    EXPECT_EQ(Load("cdrom0:\\NOPE\\MISSING.BIN;1", Buffer(), &size), 0);
    EXPECT_EQ(size, 0);
}

TEST_F(DataRead, SizeModeReportsWithoutReading) {
    DataReadIndex(root_);
    char name[] = "DIR/Sub/File.BIN";
    int  size = 0;
    EXPECT_EQ(LoadFile2(name, nullptr, &size, LOAD_FILE_SIZE), 1);
    EXPECT_EQ(size, static_cast<int>(kFileSize));
}

TEST_F(DataRead, CurrentDirectoryPrefixesRelativeNames) {
    DataReadIndex(root_);
    char dir[] = "DIR/Sub/";
    ChangeDir(dir);

    char current[64] = {};
    GetCurrentDir(current);
    EXPECT_STREQ(current, "DIR/Sub/");

    int   size = 0;
    char  relative[] = "file.bin";
    char  anchored[] = "/small.txt";
    EXPECT_EQ(LoadFile2(relative, Buffer(), &size, LOAD_FILE_READ), 1);
    EXPECT_EQ(size, static_cast<int>(kFileSize));
    EXPECT_EQ(LoadFile2(anchored, Buffer(), &size, LOAD_FILE_READ), 1);
    EXPECT_EQ(size, 10);

    ChangeDir(nullptr);
    char missing[] = "file.bin";
    EXPECT_EQ(LoadFile2(missing, Buffer(), &size, LOAD_FILE_READ), 0);

    SetCurrentDir(nullptr);
    GetCurrentDir(current);
    EXPECT_STREQ(current, "");
}

TEST_F(DataRead, PackFileIsFoundByNameInsideTheLoadedPack) {
    DataReadIndex(root_);
    int size = 0;
    ASSERT_EQ(Load("chara/pack.pac", Buffer(), &size), 1);

    auto *pack = reinterpret_cast<u_int *>(Buffer());
    EXPECT_EQ(GetPackFileNum(pack), 2);

    int   file_size = 0;
    char  name[] = "chara/b.bin";
    u_int *data = GetPackFile(pack, name, &file_size);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(file_size, 300);
    const std::vector<unsigned char> expected = MakeBytes(300, 2);
    EXPECT_EQ(std::memcmp(data, expected.data(), expected.size()), 0);
}

TEST_F(DataRead, BackgroundReadIsFinishedWhenItReturns) {
    DataReadIndex(root_);
    StartReadBG();

    char name[] = "DIR/Sub/File.BIN";
    int  size = 0;
    ASSERT_EQ(LoadFileBG(name, Buffer(), &size), 1);
    EXPECT_EQ(size, static_cast<int>(kFileSize));
    EXPECT_EQ(ReadBGSync(), 0);

    BG_READ_INFO *info = GetReadBGFile(0);
    ASSERT_NE(info, nullptr);
    EXPECT_NE(GetReadBGFile(name), nullptr);
    EXPECT_EQ(info->done, 1);
    const std::vector<unsigned char> expected = MakeBytes(kFileSize, 7);
    EXPECT_EQ(std::memcmp(info->buffer, expected.data(), kFileSize), 0);

    BreakReadBG();
    EXPECT_NE(GetReadBGFile(0), nullptr);
    StartReadBG();
    EXPECT_EQ(GetReadBGFile(0), nullptr);
}

TEST_F(DataRead, FileCacheHoldsAFileLoadedInTheBackground) {
    DataReadIndex(root_);
    std::vector<u_long128> cache(16384);
    InitFileCache(cache.data(), FILE_CACHE_UP);

    char name[] = "DIR/Sub/File.BIN";
    ASSERT_EQ(LoadFileCacheBG(name), 1);

    int        cached_size = 0;
    u_long128 *address = SearchFileCache(name, &cached_size);
    ASSERT_NE(address, nullptr);
    EXPECT_EQ(cached_size, static_cast<int>(kFileSize));

    int size = 0;
    ASSERT_EQ(Load("cdrom0:\\dir\\sub\\file.bin;1", Buffer(), &size), 1);
    EXPECT_EQ(size, static_cast<int>(kFileSize));
    const std::vector<unsigned char> expected = MakeBytes(kFileSize, 7);
    EXPECT_EQ(std::memcmp(Buffer(), expected.data(), kFileSize), 0);

    DeleteFileCache();
    EXPECT_EQ(SearchFileCache(name, &cached_size), nullptr);
}

TEST_F(DataRead, WriteToHost0LandsUnderTheSaveRoot) {
    const fs::path save = base_ / "save";
    PathsSetSaveRoot(save);
    std::vector<unsigned char> data = MakeBytes(5000, 9);
    char name[] = "host0:out/x.bin";

    ASSERT_EQ(WriteFile(name, data.data(), static_cast<int>(data.size())), 1);

    const fs::path written = save / "host0" / "out" / "x.bin";
    ASSERT_TRUE(fs::exists(written));
    EXPECT_EQ(fs::file_size(written), data.size());
    std::ifstream in(written, std::ios::binary);
    std::vector<unsigned char> back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(back, data);
}

TEST_F(DataRead, MissingRootIndexesNothingAndDoesNotAbort) {
    testing::internal::CaptureStderr();
    DataReadIndex(base_ / "absent");
    std::string log = testing::internal::GetCapturedStderr();
    EXPECT_NE(log.find("dataread: indexed 0 files under"), std::string::npos) << log;

    EXPECT_FALSE(DataReadFind("small.txt").has_value());
    int size = 0;
    EXPECT_EQ(Load("cdrom0:\\SMALL.TXT;1", Buffer(), &size), 0);
}

TEST_F(DataRead, ExtractedEditConfigLoadsWhenPresent) {
    fs::path exe = PathsExecutable();
    std::optional<fs::path> data;
    fs::path                dir = exe.parent_path();
    for (int up = 0; up < 6 && !dir.empty(); up++, dir = dir.parent_path()) {
        std::error_code error;
        if (fs::is_regular_file(dir / "data" / "edit.cfg", error)) {
            data = dir / "data";
            break;
        }
    }
    if (!data) {
        GTEST_SKIP() << "no extracted data/ above the test executable";
    }

    DataReadIndex(*data);
    std::optional<fs::path> edit = DataReadFind("cdrom0:\\EDIT.CFG;1");
    if (!edit) {
        GTEST_SKIP() << "data/ has no edit.cfg";
    }

    constexpr int kEditConfigSize = 1133;
    std::ifstream in(*edit, std::ios::binary);
    const std::vector<unsigned char> on_disk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_EQ(on_disk.size(), static_cast<std::size_t>(kEditConfigSize));

    int         size = 0;
    std::string relative = "edit.cfg";
    ASSERT_EQ(LoadFile2(relative.data(), nullptr, &size, LOAD_FILE_SIZE), 1);
    EXPECT_EQ(size, kEditConfigSize);

    std::vector<u_long128> buffer(2048 / sizeof(u_long128));
    ASSERT_EQ(Load("edit.cfg", buffer.data(), &size), 1);
    EXPECT_EQ(size, kEditConfigSize);
    EXPECT_EQ(std::memcmp(buffer.data(), on_disk.data(), on_disk.size()), 0);
}

TEST_F(DataRead, ShiftJisNameFindsItsPercentEncodedFile) {
    const std::string game = std::string("DIR\\") + '\x83' + 'A' + "%A.BIN";
    const fs::path    disk = root_ / dc::DiskName(game);
    WriteBytes(disk, MakeBytes(20, 5));

    DataReadIndex(root_);
    std::optional<fs::path> found = DataReadFind(game.c_str());
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(fs::equivalent(*found, disk));
}
