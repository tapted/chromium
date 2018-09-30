// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/views_content_client/views_content_client.h"

using namespace net::test_server;

namespace {

using Data = std::unique_ptr<base::Value>;

class VidServer {
 public:
  VidServer() {
    //net::SpawnedTestServer spawn(net::SpawnedTestServer::TYPE_HTTP, root);
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
  auto data = base::JSONReader::Read(request.content);
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
  return std::make_unique<base::ListValue>();
}

}  // namespace

int main(int argc, const char** argv) {
  ui::ViewsContentClient views_content_client(argc, argv);
  views_content_client.set_task(base::Bind(&Start));
  return views_content_client.RunMain();
}
