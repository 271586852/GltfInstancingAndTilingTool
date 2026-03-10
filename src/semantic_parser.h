#ifndef SEMANTIC_PARSER_H
#define SEMANTIC_PARSER_H

#include <string>
#include <map>
#include <vector>
#include <optional>
#include <set>
#include <filesystem>

namespace GltfInstancing {

    // 存储单个构件的语义信息
    struct SemanticInfo {
        std::string category; // Element_Category (e.g., "家具")
        std::string family;   // Element_Family   (e.g., "会议桌1")
        std::string type;     // Element_Type     (e.g., "1600x500x740mm...")
        
        // 可以根据需要扩展更多属性，如材质、系统类型等
        // std::string material; 
    };

    class SemanticParser {
    public:
        SemanticParser();
        ~SemanticParser();

        // 解析 XML (.RISCRVT) 文件（单文件模式）
        // 返回 true 表示解析成功
        bool parse(const std::string& xmlPath);

        // 从文件夹解析：根据 input_directory 下的 GLB 文件名，在 semanticDataPath 文件夹中查找同名 .RISCRVT
        // 例如 SZW_HRZB_STR_26F.glb -> semanticDataPath/SZW_HRZB_STR_26F.RISCRVT
        // glbPaths: 输入 GLB 文件路径集合
        bool parseFromFolder(const std::string& semanticDataPath, const std::set<std::filesystem::path>& glbPaths);

        // 根据 Mesh 的 Hash ID 获取语义信息（单文件模式或 glbStem 为空时）
        std::optional<SemanticInfo> getSemanticInfo(const std::string& meshHashId) const;

        // 根据 GLB 文件名 stem + Mesh Hash ID 获取语义信息（文件夹模式）
        // glbStem: GLB 文件名的 stem，如 "SZW_HRZB_STR_26F"
        std::optional<SemanticInfo> getSemanticInfo(const std::string& glbStem, const std::string& meshHashId) const;

        // 获取所有已解析的语义数据 (用于调试或统计)
        const std::map<std::string, SemanticInfo>& getAllSemantics() const;

    private:
        // 存储 HashID -> SemanticInfo 的映射（单文件模式）
        // 或 "glbStem|meshHashId" -> SemanticInfo（文件夹模式）
        std::map<std::string, SemanticInfo> _semanticMap;
        bool _folderMode = false;

        // 内部辅助函数：清理字符串 (去除空白、引号等)
        std::string cleanString(const std::string& input);
        bool parseSingleFile(const std::string& xmlPath, const std::string& prefixForKey = "");
    };

} // namespace GltfInstancing

#endif // SEMANTIC_PARSER_H

