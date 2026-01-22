#include "semantic_parser.h"
#include "utilities.h" // For logging
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace GltfInstancing {

    SemanticParser::SemanticParser() {}

    SemanticParser::~SemanticParser() {}

    std::string SemanticParser::cleanString(const std::string& input) {
        std::string output = input;
        // 去除首尾空白
        output.erase(0, output.find_first_not_of(" \t\r\n"));
        output.erase(output.find_last_not_of(" \t\r\n") + 1);
        return output;
    }

    // 一个简单的流式 XML 解析器，专门针对 .RISCRVT 格式优化
    // 不依赖庞大的第三方 XML 库，只提取我们需要的数据
    bool SemanticParser::parse(const std::string& xmlPath) {
        logMessage("Parsing semantic XML: " + xmlPath);

        std::ifstream file(xmlPath);
        if (!file.is_open()) {
            logError("Failed to open XML file: " + xmlPath);
            return false;
        }

        std::string line;
        std::string currentHashId;
        SemanticInfo currentInfo;
        bool inMetaDataBlock = false;

        // 逐行读取，状态机模式
        while (std::getline(file, line)) {
            // 1. 查找 MetaData 开始标签，提取 Hash ID
            // 格式示例: <MetaData name="HASH_DATA" reference="Actor.HASH">
            if (line.find("<MetaData") != std::string::npos) {
                size_t refPos = line.find("reference=\"Actor.");
                if (refPos != std::string::npos) {
                    size_t start = refPos + 17; // "reference=\"Actor.".length()
                    size_t end = line.find("\"", start);
                    if (end != std::string::npos) {
                        currentHashId = line.substr(start, end - start);
                        inMetaDataBlock = true;
                        // 重置当前 info
                        currentInfo = SemanticInfo();
                    }
                }
            }
            // 2. 查找 MetaData 结束标签
            else if (line.find("</MetaData>") != std::string::npos) {
                if (inMetaDataBlock && !currentHashId.empty()) {
                    // 保存当前块的数据
                    // 只有当至少有一个关键属性存在时才保存
                    if (!currentInfo.category.empty() || !currentInfo.family.empty()) {
                        _semanticMap[currentHashId] = currentInfo;
                    }
                }
                inMetaDataBlock = false;
                currentHashId.clear();
            }
            // 3. 在 MetaData 块内查找属性
            // 格式示例: <KeyValueProperty name="Element_Category" type="String" val="家具"/>
            else if (inMetaDataBlock) {
                if (line.find("<KeyValueProperty") != std::string::npos) {
                    // 提取 name
                    std::string nameAttr = "name=\"";
                    size_t namePos = line.find(nameAttr);
                    
                    // 提取 val
                    std::string valAttr = "val=\"";
                    size_t valPos = line.find(valAttr);

                    if (namePos != std::string::npos && valPos != std::string::npos) {
                        // 解析 Name
                        size_t nameStart = namePos + nameAttr.length();
                        size_t nameEnd = line.find("\"", nameStart);
                        std::string key = line.substr(nameStart, nameEnd - nameStart);

                        // 解析 Value
                        size_t valStart = valPos + valAttr.length();
                        size_t valEnd = line.find("\"", valStart);
                        std::string value = line.substr(valStart, valEnd - valStart);

                        // 填充 SemanticInfo
                        if (key == "Element_Category") {
                            currentInfo.category = value;
                        }
                        else if (key == "Element_Family") {
                            currentInfo.family = value;
                        }
                        else if (key == "Element_Type") {
                            currentInfo.type = value;
                        }
                    }
                }
            }
        }

        file.close();
        logMessage("Semantic parsing complete. Loaded info for " + std::to_string(_semanticMap.size()) + " actors.");
        return true;
    }

    std::optional<SemanticInfo> SemanticParser::getSemanticInfo(const std::string& meshHashId) const {
        auto it = _semanticMap.find(meshHashId);
        if (it != _semanticMap.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    const std::map<std::string, SemanticInfo>& SemanticParser::getAllSemantics() const {
        return _semanticMap;
    }

} // namespace GltfInstancing

