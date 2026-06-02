import os
import requests

REPO = "rustdesk/rustdesk-server"
TAG_URL = f"https://api.github.com/repos/{REPO}/releases/latest"

TARGET_FILES = [
    "hbbs_1.1.15",
    "hbbr_1.1.15"
]

def get_latest_release():
    r = requests.get(TAG_URL)
    r.raise_for_status()
    data = r.json()
    return data["tag_name"], data["assets"]

def download_file(url, path):
    with requests.get(url, stream=True) as r:
        r.raise_for_status()
        with open(path, "wb") as f:
            for chunk in r.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)

def main():
    output_dir = os.path.expanduser("~/rustdesk-server")
    os.makedirs(output_dir, exist_ok=True)

    tag, assets = get_latest_release()
    print(f"Latest version: {tag}")

    for asset in assets:
        name = asset["name"]

        # Download Linux arm64v8 zip (contains both hbbs and hbbr)
        # macOS binaries don't exist in RustDesk server releases
        if name == "rustdesk-server-linux-arm64v8.zip":
            url = asset["browser_download_url"]
            path = os.path.join(output_dir, name)
            print(f"Downloading {name}")
            download_file(url, path)
            print(f"Extracting {name}")
            import zipfile
            with zipfile.ZipFile(path, 'r') as zip_ref:
                zip_ref.extractall(output_dir)
            print(f"Making hbbs and hbbr executable")
            os.chmod(os.path.join(output_dir, "hbbs"), 0o755)
            os.chmod(os.path.join(output_dir, "hbbr"), 0o755)
            break

    print("Done. Files saved in:", output_dir)

if __name__ == "__main__":
    main()