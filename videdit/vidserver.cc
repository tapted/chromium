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
using base::Value;
using base::DictionaryValue;
using base::JSONReader;
using base::JSONWriter;

namespace {

using Data = std::unique_ptr<DictionaryValue>;

class VidServer {
 public:
  VidServer() {
    server.AddDefaultHandlers(base::FilePath("videdit"));
    server.RegisterRequestHandler(
        base::BindRepeating(&VidServer::Map, base::Unretained(this)));
    bool result = server.Start();
    assert(result);
    LOG(INFO) << "Serving at " << server.host_port_pair().ToString();
  }

  std::unique_ptr<HttpResponse> Map(const HttpRequest& request);
  std::unique_ptr<HttpResponse> Json(const Data& data);
  Data Files(const Data& data);

 private:
  net::EmbeddedTestServer server{net::EmbeddedTestServer::TYPE_HTTP};
};

VidServer* g_vid_server;

void Start(content::BrowserContext* browser_context,
           gfx::NativeWindow window_context) {
  DLOG(INFO) << "Start task.";
  g_vid_server = new VidServer;
}

std::unique_ptr<HttpResponse> VidServer::Map(const HttpRequest& request) {
  auto data = DictionaryValue::From(JSONReader::Read(request.content));
  if (request.relative_url == "/files")
    return Json(Files(data));
  LOG(INFO) << request.relative_url;
  LOG(INFO) << request.method_string;
  LOG(INFO) << request.content;
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

Data VidServer::Files(const Data& data) {
  base::FilePath path;
  if (auto* found = data->FindKeyOfType("path", Value::Type::STRING))
    ignore_result(path.Append(found->GetString()));
  else
    base::GetCurrentDirectory(&path);

  const bool kRecursive = false;
  const int kTypes = base::FileEnumerator::FILES |
                     base::FileEnumerator::DIRECTORIES |
                     base::FileEnumerator::INCLUDE_DOT_DOT;
  base::FileEnumerator files(path, kRecursive, kTypes);
  Value::ListStorage files_list;
  for (path = files.Next(); !path.empty(); path = files.Next())
    files_list.emplace_back(Value(path.value()));

  auto result = std::make_unique<DictionaryValue>();
  result->SetKey("entries", Value(std::move(files_list)));
  return result;
}

}  // namespace

int main(int argc, const char** argv) {
  ui::ViewsContentClient views_content_client(argc, argv);
  views_content_client.set_task(base::Bind(&Start));
  return views_content_client.RunMain();
}
