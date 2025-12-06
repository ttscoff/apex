# Homebrew formula for Apex
# To use this formula, create a tap:
#   brew tap ttscoff/thelab https://github.com/ttscoff/homebrew-thelab
# Then install:
#   brew install apex

class Apex < Formula
  desc "Unified Markdown processor supporting CommonMark, GFM, MultiMarkdown, and Kramdown"
  homepage "https://github.com/ttscoff/apex"
  url "https://github.com/ttscoff/apex.git",
      tag: "v0.1.10",
      revision: "d82e80354315816fc4bd675fc40bb552e8c6aaf0"
  version "0.1.10"
  license "MIT"

  depends_on "cmake" => :build

  # On macOS, Xcode command line tools are usually sufficient
  # but we can require Xcode if needed for framework builds
  on_macos do
    depends_on :xcode => :build
  end

  def install
    # Create build directory
    mkdir "build" do
      # Configure with CMake
      system "cmake", "-S", "..", "-B", ".", "-DCMAKE_BUILD_TYPE=Release"

      # Build only the CLI executable (not framework)
      system "cmake", "--build", ".", "--target", "apex_cli"

      # Install binary
      bin.install "apex"
    end
  end

  test do
    # Test basic functionality
    (testpath/"test.md").write("# Hello World\n")
    assert_match "<h1 id=\"hello-world\">Hello World</h1>", shell_output("#{bin}/apex test.md")

    # Test version
    assert_match version.to_s, shell_output("#{bin}/apex --version", 2)
  end
end

