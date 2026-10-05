// The embedded UI: what the build wrote into dbg_webui_data.cpp must be the bundle directory, file for
// file, and the server must serve it, compressed or inflated as the client asks.
//
//   test_webui [bundle directory the build embedded; empty or absent when it embedded none]

#include "client.h"

#include "debugger/dbg_net.h"
#include "debugger/dbg_webui.h"

#include "check.h"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace dbg;

struct Quiet : net::Handler {
	void OnOpen(uint32_t) override {}
	void OnText(uint32_t, const std::string&) override {}
	void OnClose(uint32_t) override {}
};

static std::string ReadAll(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static void TestBuiltin(int port) {
	CHECK(!webui::Embedded());
	CHECK_EQ(std::string(webui::Version()), std::string("none"));
	CHECK_EQ(std::string(webui::GitRev()), std::string(""));
	CHECK_EQ(webui::FileCount(), (size_t)0);
	CHECK(webui::Find("index.html") == nullptr);

	const std::string& page = webui::BuiltinPage();
	CHECK(page.find("no devtools UI embedded") != std::string::npos);
	CHECK(page.find("NULLDC_DEVTOOLS_DIST") != std::string::npos);
	CHECK(page.find("Debugger:WebRoot") != std::string::npos);
	CHECK(page.find("npm run build:native") != std::string::npos);
	CHECK(page.find("protocol 3") != std::string::npos);

	for (const char* target : { "/", "/index.html" }) {
		for (const char* headers : { "", "Accept-Encoding: gzip\r\n" }) {
			HttpResult r = TestClient::Get(port, target, headers);
			CHECK_EQ(r.status, 200);
			CHECK(r.body == page);
			CHECK_EQ(r.Header("Content-Type"), std::string("text/html; charset=utf-8"));
			CHECK_EQ(r.Header("Content-Encoding"), std::string(""));
			CHECK_EQ(r.Header("Cache-Control"), std::string("no-cache"));
		}
	}
	// only the page exists
	CHECK_EQ(TestClient::Get(port, "/assets/index.js").status, 404);
	CHECK_EQ(TestClient::Get(port, "/bundle-info.json").status, 404);
	CHECK(TestClient::Get(port, "/workspace").body == page);
}

static void TestBundle(int port, const std::string& dir) {
	namespace fs = std::filesystem;
	REQUIRE(webui::Embedded());
	std::string info = ReadAll(fs::path(dir) / "bundle-info.json");
	CHECK(std::string(webui::Version()) != "none" && webui::Version()[0] != '\0');
	CHECK(info.find("\"" + std::string(webui::Version()) + "\"") != std::string::npos);
	if (webui::GitRev()[0])
		CHECK(info.find("\"" + std::string(webui::GitRev()) + "\"") != std::string::npos);
	printf("embedded bundle: version %s, revision %s, %zu files\n", webui::Version(), webui::GitRev(), webui::FileCount());

	size_t onDisk = 0, raw = 0, stored = 0;
	for (const auto& entry : fs::recursive_directory_iterator(dir)) {
		if (!entry.is_regular_file())
			continue;
		onDisk++;
		std::string path = fs::relative(entry.path(), dir).generic_string();
		std::string content = ReadAll(entry.path());

		const webui::File* file = webui::Find(path);
		gChecks++;
		if (!file) {
			gFailures++;
			printf("%s is not in the embedded table\n", path.c_str());
			continue;
		}
		raw += file->rawSize;
		stored += file->size;
		CHECK_EQ(file->rawSize, content.size());
		// a gzip stream with the timestamp cleared, so the generated source depends on the content only
		REQUIRE(file->size >= 18);
		CHECK(file->data[0] == 0x1f && file->data[1] == 0x8b && file->data[2] == 8);
		CHECK(file->data[4] == 0 && file->data[5] == 0 && file->data[6] == 0 && file->data[7] == 0);
		std::string inflated = "stale";
		CHECK(webui::Inflate(*file, &inflated));
		CHECK(inflated == content);

		std::string target = "/";
		for (char c : path)
			target += c == ' ' ? std::string("%20") : std::string(1, c);
		const char* cache = path.compare(0, 7, "assets/") == 0 ? "max-age=31536000, immutable" : "no-cache";

		// a client that does not announce gzip gets the file itself
		HttpResult r = TestClient::Get(port, target);
		CHECK_EQ(r.status, 200);
		CHECK(r.body == content);
		CHECK_EQ(r.Header("Content-Encoding"), std::string(""));
		CHECK_EQ(r.Header("Content-Length"), std::to_string(content.size()));
		CHECK_EQ(r.Header("Cache-Control"), std::string(cache));
		CHECK_EQ(r.Header("Vary"), std::string("Accept-Encoding"));
		CHECK_EQ(r.Header("X-Content-Type-Options"), std::string("nosniff"));

		// a browser gets the stored stream
		r = TestClient::Get(port, target, "Accept-Encoding: gzip, deflate, br\r\n");
		CHECK_EQ(r.status, 200);
		CHECK(r.body == std::string((const char*)file->data, file->size));
		CHECK_EQ(r.Header("Content-Encoding"), std::string("gzip"));
		CHECK_EQ(r.Header("Content-Length"), std::to_string(file->size));
		CHECK_EQ(r.Header("Cache-Control"), std::string(cache));

		r = TestClient::Get(port, target, "Accept-Encoding: gzip;q=0, identity\r\n");
		CHECK(r.body == content);
		CHECK_EQ(r.Header("Content-Encoding"), std::string(""));
	}
	CHECK_EQ(webui::FileCount(), onDisk);
	CHECK(onDisk >= 2);		// index.html and bundle-info.json at least
	printf("%zu files, %zu bytes, %zu bytes stored\n", onDisk, raw, stored);

	// the table is exactly Files()[0 .. FileCount()), in path order, ended by a null path
	const webui::File* files = webui::Files();
	for (size_t i = 0; i + 1 < webui::FileCount(); i++)
		CHECK(std::string(files[i].path) < std::string(files[i + 1].path));
	CHECK(files[webui::FileCount()].path == nullptr);

	std::string index = ReadAll(fs::path(dir) / "index.html");
	HttpResult r = TestClient::Get(port, "/");
	CHECK(r.body == index);
	CHECK_EQ(r.Header("Content-Type"), std::string("text/html; charset=utf-8"));
	CHECK_EQ(r.Header("Cache-Control"), std::string("no-cache"));
	CHECK_EQ(TestClient::Get(port, "/?ws=ws://127.0.0.1:1/ws").body, index);

	// a stale asset name is a 404, not the page; a route of the page is the page
	CHECK(webui::Find("assets/index-stale000.js") == nullptr);
	CHECK(webui::Find("") == nullptr);
	CHECK(webui::Find("/index.html") == nullptr);
	CHECK_EQ(TestClient::Get(port, "/assets/index-stale000.js").status, 404);
	r = TestClient::Get(port, "/workspace");
	CHECK(r.body == index);
	CHECK_EQ(r.Header("Cache-Control"), std::string("no-cache"));
}

int main(int argc, char** argv) {
	std::string dir = argc > 1 ? argv[1] : "";
	CHECK_EQ(kProtocolVersion, 3);

	Quiet handler;
	net::Server server;
	net::Options options;
	options.port = 0;
	REQUIRE(server.Start(options, &handler));
	if (dir.empty())
		TestBuiltin(server.Port());
	else
		TestBundle(server.Port(), dir);
	server.Stop();
	return Finish("test_webui");
}
