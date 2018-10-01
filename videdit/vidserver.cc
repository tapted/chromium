// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/bind.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/views_content_client/views_content_client.h"

using namespace net::test_server;
using base::DictionaryValue;
using base::JSONReader;
using base::JSONWriter;
using base::ListValue;
using base::Value;

namespace {

using Data = std::unique_ptr<DictionaryValue>;

class VidServer {
 public:
  VidServer() {
    base::GetCurrentDirectory(&base_path);
    server.AddDefaultHandlers(base::FilePath("videdit"));
    server.RegisterRequestHandler(
        base::BindRepeating(&VidServer::Map, base::Unretained(this)));
    bool result = server.Start();
    assert(result);
    LOG(INFO) << "Serving at " << server.host_port_pair().ToString();
  }

  std::unique_ptr<HttpResponse> Map(const HttpRequest& request);
  std::unique_ptr<HttpResponse> Json(const Data& data);

  std::string MakeRelative(const std::string& absolute);
  base::FilePath ToPath(const Data& data);
  ListValue Entries(const base::FilePath& folder, int types);
  Data Files(const Data& data);
  Data Open(const Data& data);

 private:
  base::FilePath base_path;
  net::EmbeddedTestServer server{net::EmbeddedTestServer::TYPE_HTTP};
};

VidServer* g_vid_server;

void Start(content::BrowserContext* browser_context,
           gfx::NativeWindow window_context) {
  DLOG(INFO) << "Start task.";
  g_vid_server = new VidServer;
}

std::unique_ptr<HttpResponse> VidServer::Map(const HttpRequest& request) {
  LOG(INFO) << request.relative_url;
  LOG(INFO) << request.content;
  auto data = DictionaryValue::From(JSONReader::Read(request.content));
  if (request.relative_url == "/files")
    return Json(Files(data));
  if (request.relative_url == "/open")
    return Json(Open(data));
  return nullptr;
}

std::unique_ptr<HttpResponse> VidServer::Json(const Data& data) {
  auto response = std::make_unique<BasicHttpResponse>();
  response->set_content_type("application/json; charset=utf-8");
  std::string json;
  base::JSONWriter::Write(*data, &json);
  response->set_content(json);
  return std::move(response);
}

std::string VidServer::MakeRelative(const std::string& absolute) {
  // Use CreateFilePathValue?
  if (absolute.empty() || absolute[0] != '/')
    return absolute;
  if (absolute.find(base_path.value()) == 0)
    return absolute.substr(base_path.value().size() + 1);
  return absolute.substr(1);
}

base::FilePath VidServer::ToPath(const Data& data) {
  if (auto* found = data->FindKeyOfType("path", Value::Type::STRING))
    return base_path.Append(MakeRelative(found->GetString()));
  return base_path;
}

ListValue VidServer::Entries(const base::FilePath& folder, int types) {
  const bool kRecursive = false;
  base::FileEnumerator files(folder, kRecursive, types);
  Value::ListStorage files_list;
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    auto info = files.GetInfo();
    Value dict(Value::Type::DICTIONARY);
    dict.SetKey("path", Value(MakeRelative(path.value())));
    dict.SetKey("size", Value(static_cast<double>(info.GetSize())));
    dict.SetKey("isDir", Value(info.IsDirectory()));
    dict.SetKey("mtime", Value(info.GetLastModifiedTime().ToJsTime()));
    files_list.emplace_back(std::move(dict));
  }
  return ListValue(std::move(files_list));
}

Data VidServer::Files(const Data& data) {
  base::FilePath path = ToPath(data);
  LOG(INFO) << "Reading: " << path.value();

  auto result = std::make_unique<DictionaryValue>();
  result->SetKey("folders", Entries(path, base::FileEnumerator::DIRECTORIES));
  result->SetKey("entries", Entries(path, base::FileEnumerator::FILES));
  return result;
}

Data VidServer::Open(const Data& data) {
  base::FilePath path = ToPath(data);
  LOG(INFO) << "Opening: " << path.value();
  auto result = std::make_unique<DictionaryValue>();
  return result;
}

}  // namespace

int main(int argc, const char** argv) {
  ui::ViewsContentClient views_content_client(argc, argv);
  views_content_client.set_task(base::Bind(&Start));
  return views_content_client.RunMain();
}
