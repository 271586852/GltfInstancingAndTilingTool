#ifndef SEMANTIC_PARSER_H
#define SEMANTIC_PARSER_H

#include <string>
#include <map>
#include <vector>
#include <optional>

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

        // 解析 XML (.RISCRVT) 文件
        // 返回 true 表示解析成功
        bool parse(const std::string& xmlPath);

        // 根据 Mesh 的 Hash ID (对应 XML 中的 Actor.Hash) 获取语义信息
        // 返回 std::nullopt 表示未找到
        std::optional<SemanticInfo> getSemanticInfo(const std::string& meshHashId) const;

        // 获取所有已解析的语义数据 (用于调试或统计)
        const std::map<std::string, SemanticInfo>& getAllSemantics() const;

    private:
        // 存储 HashID -> SemanticInfo 的映射
        std::map<std::string, SemanticInfo> _semanticMap;

        // 内部辅助函数：清理字符串 (去除空白、引号等)
        std::string cleanString(const std::string& input);
    };

} // namespace GltfInstancing

#endif // SEMANTIC_PARSER_H

