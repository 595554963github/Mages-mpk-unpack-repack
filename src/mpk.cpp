namespace mpk {
    constexpr uint32_t MPK_MAGIC = fourCC('M', 'P', 'K', '\0');

    struct mpk_header {
        uint32_t magic{};
        uint32_t version{};
        uint64_t entries{};
        char padding[0x30]{};
    };

    struct mpk_entry {
        uint32_t compression{};
        uint32_t entry_id{};
        uint64_t offset{};
        uint64_t size{};
        uint64_t size_decompressed{};
        char filename[0xE0]{};

        const std::string to_unpacked_filename(const std::string& base_name, uint32_t index, const std::string& extension = "") const {
            std::stringstream ss;
            ss << base_name << (index + 1) << extension;
            return ss.str();
        }
    };
}

int main(int argc, char* argv[])
{
    argh::parser cmdl(argv, argh::parser::Mode::PREFER_PARAM_FOR_UNREG_OPTION);

    struct {
        std::string infile;
        std::string outdir;
        bool extract = false;
        bool create = false;
    } args;

    auto c_extract = cmdl({ "e", "extract" });
    auto c_create = cmdl({ "c", "create" });

    if (!(c_extract || c_create)) {
        std::cerr << "MAGES package -MPK解包/打包工具\n";
        std::cerr << "已针对STEINS;GATE Steam版和STEINS;GATE 0 Steam版MPK文件测试\n";
        std::cerr << "注意:\n";
        std::cerr << "  - 解包后的文件按基础名称+序号命名\n";
        std::cerr << "使用方法: " << argv[0] << " -e <mpk文件>  # 将MPK提取到同名文件夹\n";
        std::cerr << "       " << argv[0] << " -c <文件夹>    # 把文件夹打包成MPK文件\n";
        return EXIT_FAILURE;
    }

    if (c_extract) {
        args.extract = true;
        c_extract >> args.infile;
        std::filesystem::path input_path(args.infile);
        args.outdir = (input_path.parent_path() / input_path.stem()).string();
    }

    if (c_create) {
        args.create = true;
        c_create >> args.infile; 
        std::filesystem::path input_path(args.infile);
        args.outdir = (input_path.parent_path() / input_path.filename()).string() + ".mpk";
    }

    {
        using namespace std::filesystem;

        if (args.create) {
            std::vector<std::pair<mpk::mpk_entry, path>> entries;
            size_t buffer_size = 0;
            CHECK(exists(args.infile) && is_directory(args.infile), "无效的输入目录");

            std::vector<path> files;
            for (auto& entry : directory_iterator(args.infile)) {
                if (is_regular_file(entry)) {
                    files.push_back(entry.path());
                }
            }

            std::sort(files.begin(), files.end());

            for (size_t i = 0; i < files.size(); i++) {
                mpk::mpk_entry entry{};
                entry.entry_id = i;

                std::string filename = files[i].filename().string();
                size_t copy_len = std::min(filename.size(), sizeof(entry.filename) - 1);
                std::copy_n(filename.c_str(), copy_len, entry.filename);
                entry.filename[copy_len] = '\0';

                entries.push_back({ entry, files[i] });
                buffer_size = std::max(buffer_size, file_size(files[i]));
            }

            u8vec buffer(buffer_size);
            path output = path(args.outdir);
            if (output.has_parent_path() && !exists(output.parent_path()))
                create_directories(output.parent_path());

            FILE* fp = fopen(output.string().c_str(), "wb");
            CHECK(fp, "无法打开输出文件。");

            mpk::mpk_header hdr{};
            hdr.magic = mpk::MPK_MAGIC;
            hdr.version = 0x020000;
            hdr.entries = entries.size();
            fwrite(&hdr, sizeof(hdr), 1, fp);
            fseek(fp, hdr.entries * sizeof(mpk::mpk_entry), SEEK_CUR);
            fseek(fp, alignUp(ftell(fp), 2048), SEEK_SET);

            for (auto& [entry, path] : entries) {
                FILE* fp_in = fopen(path.string().c_str(), "rb");
                entry.offset = ftell(fp);
                entry.size_decompressed = entry.size = file_size(path);
                fread(buffer.data(), 1, entry.size, fp_in);
                fwrite(buffer.data(), 1, entry.size, fp);
                fseek(fp, alignUp(ftell(fp), 2048), SEEK_SET);
                fclose(fp_in);
            }

            fseek(fp, sizeof(hdr), SEEK_SET);
            for (auto& [entry, path] : entries) fwrite(&entry, sizeof(mpk::mpk_entry), 1, fp);
            fclose(fp);
        }
        else if (args.extract) {
            mpk::mpk_header hdr;

            FILE* fp = fopen(args.infile.c_str(), "rb");
            fread(&hdr, sizeof(hdr), 1, fp);
            CHECK(hdr.magic == mpk::MPK_MAGIC);

            if (!exists(args.outdir))
                create_directories(args.outdir);

            std::vector<mpk::mpk_entry> entries(hdr.entries);
            fread(entries.data(), sizeof(mpk::mpk_entry), hdr.entries, fp);

            std::filesystem::path mpk_path(args.infile);
            std::string base_name = mpk_path.stem().string();

            u8vec buffer;
            for (uint32_t i = 0; i < entries.size(); i++) {
                const auto& entry = entries[i];

                std::string original_filename(entry.filename);
                std::string extension = std::filesystem::path(original_filename).extension().string();

                if (extension.empty()) {
                    extension = ".bin"; 
                }

                std::string output_filename = base_name + std::to_string(i + 1) + extension;
                path output = path(args.outdir) / path(output_filename);

                if (output.has_parent_path() && !exists(output.parent_path()))
                    create_directories(output.parent_path());

                FILE* fp_out = fopen(output.string().c_str(), "wb");
                fseek(fp, entry.offset, SEEK_SET);
                buffer.resize(entry.size);
                fread(buffer.data(), 1, entry.size, fp);
                fwrite(buffer.data(), 1, entry.size, fp_out);
                fclose(fp_out);
            }
            fclose(fp);
        }
    }
    return EXIT_SUCCESS;
}