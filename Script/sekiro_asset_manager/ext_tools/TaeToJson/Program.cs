/// <summary>
/// 从 .tae 文件提取事件数据并导出为 JSON 格式。
///
/// 用法:
///   TaeToJson <tae_directory> <output.json> [--template TAE.Template.SDT.xml]
///
/// 输出 JSON 格式:
/// {
///   "TotalTaeFiles": N,
///   "TAE_Files": [
///     {
///       "FileName": "a00.tae",
///       "Animations": [
///         {
///           "AnimID": 200000,
///           "Events": [
///             {
///               "Type": 0,
///               "StartFrame": 0,
///               "EndFrame": 30,
///               "Parameters": { "JumpTableID": 115, ... }
///             }
///           ]
///         }
///       ]
///     }
///   ]
/// }
/// </summary>

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using SoulsFormats;
// TAE types are in SoulsFormats namespace

namespace TaeToJson
{
    class Program
    {
        static int Main(string[] args)
        {
            if (args.Length < 2)
            {
                Console.Error.WriteLine("用法: TaeToJson <tae_directory> <output.json> [--template TAE.Template.SDT.xml]");
                return 1;
            }

            string taeDir = args[0];
            string outputPath = args[1];
            string templatePath = null;

            for (int i = 2; i < args.Length; i++)
            {
                if (args[i] == "--template" && i + 1 < args.Length)
                    templatePath = args[i + 1];
            }

            if (!Directory.Exists(taeDir))
            {
                Console.Error.WriteLine($"TAE 目录不存在: {taeDir}");
                return 1;
            }

            // 加载 TAE 模板
            TAE.Template template = null;
            if (templatePath != null && File.Exists(templatePath))
            {
                template = TAE.Template.ReadXMLFile(templatePath);
                Console.WriteLine($"已加载模板: {templatePath}");
            }
            else
            {
                Console.WriteLine("未指定模板，事件参数将使用原始字节");
            }

            // 递归搜索所有 .tae 文件
            string[] taeFiles = Directory.GetFiles(taeDir, "*.tae", SearchOption.AllDirectories);
            Console.WriteLine($"找到 {taeFiles.Length} 个 TAE 文件");

            var result = new Dictionary<string, object>
            {
                ["TotalTaeFiles"] = taeFiles.Length,
                ["TAE_Files"] = new List<Dictionary<string, object>>()
            };

            var taeFilesList = (List<Dictionary<string, object>>)result["TAE_Files"];

            foreach (string taePath in taeFiles)
            {
                string fileName = Path.GetFileName(taePath);
                Console.WriteLine($"  处理: {fileName}");

                try
                {
                    TAE tae = TAE.Read(taePath);

                    if (template != null)
                    {
                        try { tae.ApplyTemplate(template); }
                        catch (Exception ex)
                        {
                            Console.WriteLine($"  模板应用失败: {ex.Message}");
                        }
                    }

                    var taeFileObj = new Dictionary<string, object>
                    {
                        ["FileName"] = fileName,
                        ["Animations"] = new List<Dictionary<string, object>>()
                    };

                    var animsList = (List<Dictionary<string, object>>)taeFileObj["Animations"];

                    foreach (var anim in tae.Animations)
                    {
                        var animObj = new Dictionary<string, object>
                        {
                            ["AnimID"] = (int)anim.ID,
                            ["Events"] = new List<Dictionary<string, object>>()
                        };

                        var eventsList = (List<Dictionary<string, object>>)animObj["Events"];

                        foreach (var evt in anim.Events)
                        {
                            var evtObj = new Dictionary<string, object>
                            {
                                ["Type"] = evt.Type,
                                ["StartFrame"] = (int)Math.Round(evt.StartTime * 30),
                                ["EndFrame"] = (int)Math.Round(evt.MemeEndTime * 30),
                                ["Parameters"] = new Dictionary<string, object>()
                            };

                            var paramObj = (Dictionary<string, object>)evtObj["Parameters"];

                            if (evt.Parameters != null)
                            {
                                foreach (var kvp in evt.Parameters.Values)
                                {
                                    string key = kvp.Key;
                                    object val = kvp.Value;

                                    if (val is float f) val = f;
                                    else if (val is double d) val = d;
                                    else if (val is int i) val = i;
                                    else if (val is long l) val = l;
                                    else if (val is bool b) val = b;
                                    else if (val is byte[] ba) val = Convert.ToHexString(ba);
                                    else val = val?.ToString() ?? "";

                                    paramObj[key] = val;
                                }
                            }

                            eventsList.Add(evtObj);
                        }

                        animsList.Add(animObj);
                    }

                    taeFilesList.Add(taeFileObj);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  解析失败: {ex.Message}");
                    taeFilesList.Add(new Dictionary<string, object>
                    {
                        ["FileName"] = fileName,
                        ["Animations"] = new List<Dictionary<string, object>>(),
                        ["Error"] = ex.Message
                    });
                }
            }

            var options = new JsonSerializerOptions
            {
                WriteIndented = true,
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase
            };

            string json = JsonSerializer.Serialize(result, options);
            File.WriteAllText(outputPath, json);

            Console.WriteLine($"\n完成！已导出 {taeFilesList.Count} 个 TAE 文件到: {outputPath}");
            return 0;
        }
    }
}
