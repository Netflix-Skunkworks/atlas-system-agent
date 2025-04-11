import os
import shutil

from conan import ConanFile
from conan.tools.files import download, unzip, check_sha256


class AtlasSystemAgentConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = (
        "abseil/20240116.2",
        "asio/1.32.0",
        "backward-cpp/1.6",
        "fmt/11.0.2",
        "gtest/1.15.0",
        "libcurl/8.10.1",
        "openssl/3.3.2",
        "rapidjson/cci.20230929",
        "sdbus-cpp/2.0.0",
        "spdlog/1.15.0",
        "zlib/1.3.1",
    )
    tool_requires = ()
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        # TODO: remove this when SystemD updates package for zstd
        self.requires("zstd/1.5.7", override=True)

    def configure(self):
        self.options["libcurl"].with_c_ares = True
        self.options["libcurl"].with_ssl = "openssl"

    @staticmethod
    def maybe_remove_dir(path: str):
        if os.path.isdir(path):
            shutil.rmtree(path)

    @staticmethod
    def maybe_remove_file(path: str):
        if os.path.isfile(path):
            os.unlink(path)

    def get_spectator_cpp(self):
        repo = "Netflix/spectator-cpp"
        commit = "6e7c2fd5f26085aa75a978fc5eff28805bbaf0cc"
        zip_name = repo.replace("Netflix/", "") + f"-{commit}.zip"

        self.maybe_remove_file(zip_name)
        download(self, f"https://github.com/{repo}/archive/{commit}.zip", zip_name)
        check_sha256(self, zip_name, "1605b3a4a8deb9f1f1ce742f1d95b3a1aefae32289ee7df585525c87f2a51193")

        dir_name = repo.replace("Netflix/", "")
        self.maybe_remove_dir(dir_name)
        unzip(self, zip_name, destination=dir_name, strip_root=True)
        self.maybe_remove_dir("lib/spectator")
        shutil.move(f"{dir_name}/spectator", "lib/spectator")
        self.maybe_remove_dir("lib/tools")
        shutil.move(f"{dir_name}/tools", "lib/tools")

        os.unlink(zip_name)
        shutil.rmtree(dir_name)

    def source(self):
        self.get_spectator_cpp()
