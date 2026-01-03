# Homebrew formula for Apex
# To use this formula, create a tap:
#   brew tap ttscoff/thelab https://github.com/ttscoff/homebrew-thelab
# Then install:
#   brew install apex

class Apex < Formula
  desc "Unified Markdown processor supporting CommonMark, GFM, MultiMarkdown, and Kramdown"
  homepage "https://github.com/ApexMarkdown/apex"
  version "0.1.44"
  license "MIT"

  depends_on "libyaml"

  on_macos do
    url "https://github.com/ApexMarkdown/apex/releases/download/v#{version}/apex-#{version}-macos-universal.tar.gz"
    sha256 "7d838dd4fa76bf16141283b552899f4007952ac97f7dd9dc447745beefd40ce8"
  end

  def install
    bin.install "apex"
    # Fix libyaml path to point to Homebrew's libyaml
    # This handles both Apple Silicon (/opt/homebrew) and Intel (/usr/local) installations
    libyaml_path = "#{HOMEBREW_PREFIX}/lib/libyaml-0.2.dylib"
    if File.exist?(libyaml_path)
      system "install_name_tool", "-change",
             "/Users/runner/work/apex/apex/deps/libyaml-universal/lib/libyaml-0.2.dylib",
             libyaml_path,
             bin/"apex"
    end
  end

  test do
    (testpath / "test.md").write("# Hello World\n")
    assert_match "<h1 id=\"hello-world\">Hello World</h1>", shell_output("#{bin}/apex test.md")
    assert_match version.to_s, shell_output("#{bin}/apex --version", 2)
  end
end
