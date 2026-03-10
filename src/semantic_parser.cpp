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

    bool SemanticParser::parseSingleFile(const std::string& xmlPath, const std::string& prefixForKey) {
        std::ifstream file(xmlPath);
        if (!file.is_open()) {
            logError("Failed to open XML file: " + xmlPath);
            return false;
        }

        std::string line;
        std::string currentHashId;
        SemanticInfo currentInfo;
        bool inMetaDataBlock = false;

        while (std::getline(file, line)) {
            if (line.find("<MetaData") != std::string::npos) {
                size_t refPos = line.find("reference=\"Actor.");
                if (refPos != std::string::npos) {
                    size_t start = refPos + 17;
                    size_t end = line.find("\"", start);
                    if (end != std::string::npos) {
                        currentHashId = line.substr(start, end - start);
                        inMetaDataBlock = true;
                        currentInfo = SemanticInfo();
                    }
                }
            }
            else if (line.find("</MetaData>") != std::string::npos) {
                if (inMetaDataBlock && !currentHashId.empty()) {
                    if (!currentInfo.category.empty() || !currentInfo.family.empty()) {
                        std::string key = prefixForKey.empty() ? currentHashId : (prefixForKey + "|" + currentHashId);
                        _semanticMap[key] = currentInfo;
                    }
                }
                inMetaDataBlock = false;
                currentHashId.clear();
            }
            else if (inMetaDataBlock) {
                if (line.find("<KeyValueProperty") != std::string::npos) {
                    std::string nameAttr = "name=\"";
                    size_t namePos = line.find(nameAttr);
                    std::string valAttr = "val=\"";
                    size_t valPos = line.find(valAttr);

                    if (namePos != std::string::npos && valPos != std::string::npos) {
                        size_t nameStart = namePos + nameAttr.length();
                        size_t nameEnd = line.find("\"", nameStart);
                        std::string key = line.substr(nameStart, nameEnd - nameStart);

                        size_t valStart = valPos + valAttr.length();
                        size_t valEnd = line.find("\"", valStart);
                        std::string value = line.substr(valStart, valEnd - valStart);

                        if (key == "Element_Category") currentInfo.category = value;
                        else if (key == "Element_Family") currentInfo.family = value;
                        else if (key == "Element_Type") currentInfo.type = value;
                    }
                }
            }
        }

        file.close();
        return true;
    }

    bool SemanticParser::parse(const std::string& xmlPath) {
        logMessage("Parsing semantic XML: " + xmlPath);
        _folderMode = false;
        _semanticMap.clear();
        bool ok = parseSingleFile(xmlPath, "");
        if (ok) logMessage("Semantic parsing complete. Loaded info for " + std::to_string(_semanticMap.size()) + " actors.");
        return ok;
    }

    bool SemanticParser::parseFromFolder(const std::string& semanticDataPath, const std::set<std::filesystem::path>& glbPaths) {
        logMessage("Parsing semantic data from folder: " + semanticDataPath + " (matching GLB filenames)");
        _folderMode = true;
        _semanticMap.clear();

        std::filesystem::path basePath(semanticDataPath);
        if (!std::filesystem::exists(basePath) || !std::filesystem::is_directory(basePath)) {
            logError("Semantic data path is not a valid directory: " + semanticDataPath);
            return false;
        }

        size_t totalLoaded = 0;
        for (const auto& glbPath : glbPaths) {
            std::string stem = glbPath.stem().string();
            std::filesystem::path riscrvtPath = basePath / (stem + ".RISCRVT");
            if (!std::filesystem::exists(riscrvtPath)) continue;

            size_t before = _semanticMap.size();
            if (parseSingleFile(riscrvtPath.string(), stem)) {
                totalLoaded += _semanticMap.size() - before;
            }
        }

        logMessage("Semantic parsing complete. Loaded info for " + std::to_string(_semanticMap.size()) + " actors from " + std::to_string(glbPaths.size()) + " GLB(s).");
        return !_semanticMap.empty();
    }

    std::optional<SemanticInfo> SemanticParser::getSemanticInfo(const std::string& meshHashId) const {
        auto it = _semanticMap.find(meshHashId);
        if (it != _semanticMap.end()) return it->second;
        return std::nullopt;
    }

    std::optional<SemanticInfo> SemanticParser::getSemanticInfo(const std::string& glbStem, const std::string& meshHashId) const {
        if (!glbStem.empty()) {
            std::string key = glbStem + "|" + meshHashId;
            auto it = _semanticMap.find(key);
            if (it != _semanticMap.end()) return it->second;
        }
        return getSemanticInfo(meshHashId);
    }

    const std::map<std::string, SemanticInfo>& SemanticParser::getAllSemantics() const {
        return _semanticMap;
    }

} // namespace GltfInstancing

