#!/usr/bin/env python3
import os
import json
import sqlite3
import requests
import zipfile
import argparse
import time
from datetime import datetime
from difflib import SequenceMatcher
from pathlib import Path

GITHUB_SEARCH = "https://api.github.com/search/repositories?q="
GITHUB_RAW = "https://raw.githubusercontent.com"
DB_PATH = os.path.expanduser("~/concept_index.db")
DOWNLOAD_DIR = os.path.expanduser("~/concept_downloads")

class ConceptIndexer:
    def __init__(self, db_path=None, download_dir=None):
        self.db_path = db_path or DB_PATH
        self.download_dir = download_dir or DOWNLOAD_DIR
        self.session = requests.Session()
        self.session.headers.update({
            'Accept': 'application/vnd.github.v3+json',
            'User-Agent': 'ConceptSemanticRepoIndexer'
        })
        os.makedirs(self.download_dir, exist_ok=True)

    def init_db(self):
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        c.execute("""
            CREATE TABLE IF NOT EXISTS items (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT,
                url TEXT,
                description TEXT,
                readme_snippet TEXT,
                local_zip_path TEXT,
                extracted_path TEXT,
                keywords TEXT,
                language TEXT,
                stars INTEGER,
                created_at TEXT,
                indexed_at TEXT
            )
        """)
        c.execute("""
            CREATE INDEX IF NOT EXISTS idx_name ON items(name)
        """)
        c.execute("""
            CREATE INDEX IF NOT EXISTS idx_keywords ON items(keywords)
        """)
        conn.commit()
        conn.close()

    def search_github(self, query, limit=10):
        try:
            params = {
                'q': query,
                'sort': 'stars',
                'order': 'desc',
                'per_page': limit
            }
            r = self.session.get(GITHUB_SEARCH, params=params, timeout=30)
            r.raise_for_status()
            return r.json().get("items", [])
        except requests.RequestException as e:
            print(f"[ERROR] GitHub search failed: {e}")
            return []

    def download_repo_zip(self, repo):
        name = repo["name"]
        owner = repo["owner"]["login"]
        default_branch = repo.get("default_branch", "main")
        
        url = f"{GITHUB_RAW}/{owner}/{name}/{default_branch}/README.md"
        zip_url = repo["html_url"] + "/archive/refs/heads/" + default_branch + ".zip"
        
        zip_path = os.path.join(self.download_dir, f"{name}.zip")
        extract_path = os.path.join(self.download_dir, name)
        
        try:
            print(f"[DOWNLOAD] {name} from {zip_url}")
            r = self.session.get(zip_url, timeout=60, stream=True)
            if r.status_code != 200:
                print(f"[SKIP] {name} - ZIP download failed (status {r.status_code})")
                return None
            
            with open(zip_path, "wb") as f:
                for chunk in r.iter_content(chunk_size=8192):
                    if chunk:
                        f.write(chunk)
            
            print(f"[EXTRACT] {name}")
            with zipfile.ZipFile(zip_path, "r") as z:
                z.extractall(extract_path)
            
            readme_snippet = self._extract_readme(owner, name, default_branch)
            
            return zip_path, extract_path, readme_snippet
        except Exception as e:
            print(f"[ERROR] Failed to download {name}: {e}")
            if os.path.exists(zip_path):
                os.remove(zip_path)
            return None

    def _extract_readme(self, owner, name, branch):
        try:
            url = f"{GITHUB_RAW}/{owner}/{name}/{branch}/README.md"
            r = self.session.get(url, timeout=10)
            if r.status_code == 200:
                content = r.text
                return content[:500] if len(content) > 500 else content
        except:
            pass
        return ""

    def extract_metadata(self, repo):
        desc = repo.get("description") or ""
        name = repo.get("name") or ""
        url = repo.get("html_url")
        language = repo.get("language") or ""
        stars = repo.get("stargazers_count", 0)
        
        return {
            "name": name,
            "url": url,
            "description": desc,
            "language": language,
            "stars": stars,
            "keywords": " ".join(repo.get("topics", []))
        }

    def store_item(self, meta, zip_path, extract_path, readme_snippet):
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        
        try:
            c.execute("""
                INSERT INTO items (
                    name, url, description, readme_snippet,
                    local_zip_path, extracted_path, keywords,
                    language, stars, created_at, indexed_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                meta["name"],
                meta["url"],
                meta["description"],
                readme_snippet,
                zip_path,
                extract_path,
                meta["keywords"],
                meta["language"],
                meta["stars"],
                str(datetime.now()),
                str(datetime.now())
            ))
            conn.commit()
            print(f"[STORED] {meta['name']} in database")
        except sqlite3.IntegrityError:
            print(f"[SKIP] {meta['name']} already exists")
        finally:
            conn.close()

    def score(self, query, text):
        if not text:
            return 0.0
        query_lower = query.lower()
        text_lower = text.lower()
        
        base_score = SequenceMatcher(None, query_lower, text_lower).ratio()
        
        keyword_bonus = 0
        query_words = set(query_lower.split())
        text_words = set(text_lower.split())
        
        for qw in query_words:
            if qw in text_words:
                keyword_bonus += 0.1
        
        return min(base_score + keyword_bonus, 1.0)

    def query_db(self, q, limit=10):
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        
        c.execute("""
            SELECT name, url, description, readme_snippet, keywords, language, stars
            FROM items
        """)
        rows = c.fetchall()
        
        scored = []
        for r in rows:
            blob = " ".join(str(x) for x in r if x)
            s = self.score(q, blob)
            scored.append((s, r))
        
        scored.sort(reverse=True, key=lambda x: x[0])
        conn.close()
        return scored[:limit]

    def run_search(self, query, limit=5, download=True):
        self.init_db()
        
        print(f"[SEARCH] Query: '{query}'")
        repos = self.search_github(query, limit)
        
        if not repos:
            print("[INFO] No repositories found")
            return
        
        print(f"[FOUND] {len(repos)} repositories")
        
        if download:
            for i, repo in enumerate(repos, 1):
                print(f"\n[{i}/{len(repos)}] Processing: {repo['name']}")
                meta = self.extract_metadata(repo)
                result = self.download_repo_zip(repo)
                
                if result:
                    zip_path, extract_path, readme_snippet = result
                    self.store_item(meta, zip_path, extract_path, readme_snippet)
                else:
                    print(f"[SKIP] {meta['name']}")
                
                time.sleep(1)
        
        print("\n[DB SEARCH RESULTS]\n")
        results = self.query_db(query, limit)
        
        if not results:
            print("[INFO] No results in database")
            return
        
        for i, (score_val, data) in enumerate(results, 1):
            name, url, description, readme, keywords, language, stars = data
            print(f"[{i}] Score: {score_val:.3f}")
            print(f"    Name: {name}")
            print(f"    URL: {url}")
            print(f"    Stars: {stars} | Language: {language}")
            print(f"    Description: {description[:100]}...")
            if keywords:
                print(f"    Keywords: {keywords}")
            print()

    def list_items(self, limit=20):
        self.init_db()
        conn = sqlite3.connect(self.db_path)
        c = conn.cursor()
        
        c.execute("""
            SELECT name, url, language, stars, created_at
            FROM items
            ORDER BY stars DESC
            LIMIT ?
        """, (limit,))
        
        rows = c.fetchall()
        conn.close()
        
        if not rows:
            print("[INFO] Database is empty")
            return
        
        print(f"[DATABASE] {len(rows)} items (showing top {limit})\n")
        for i, (name, url, language, stars, created) in enumerate(rows, 1):
            print(f"[{i}] {name}")
            print(f"    URL: {url}")
            print(f"    Language: {language} | Stars: {stars}")
            print(f"    Added: {created}")
            print()

def main():
    parser = argparse.ArgumentParser(
        description="Concept Semantic Repo Indexer - Search, download, and index GitHub repositories"
    )
    parser.add_argument(
        "query",
        nargs="*",
        help="Search query (concept-based)"
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=5,
        help="Number of repositories to search/download (default: 5)"
    )
    parser.add_argument(
        "--no-download",
        action="store_true",
        help="Search database only, don't download new repos"
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List all items in database"
    )
    parser.add_argument(
        "--db-path",
        default=None,
        help="Custom database path"
    )
    parser.add_argument(
        "--download-dir",
        default=None,
        help="Custom download directory"
    )
    
    args = parser.parse_args()
    
    indexer = ConceptIndexer(
        db_path=args.db_path,
        download_dir=args.download_dir
    )
    
    if args.list:
        indexer.list_items(limit=args.limit)
    elif args.query:
        query = " ".join(args.query)
        indexer.run_search(query, limit=args.limit, download=not args.no_download)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
