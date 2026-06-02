#!/usr/bin/env python3
import sys
import os
import sqlite3
from pathlib import Path
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLineEdit, QPushButton, QTextEdit, QLabel, QSplitter,
    QTabWidget, QFileDialog, QMessageBox, QCheckBox, QComboBox,
    QProgressBar, QStatusBar, QTreeWidget, QTreeWidgetItem
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QFont

DB_PATH = os.path.expanduser("~/.cascade_search/app.db")

class SearchThread(QThread):
    results_found = pyqtSignal(list)
    finished = pyqtSignal()
    
    def __init__(self, query, search_content=False):
        super().__init__()
        self.query = query
        self.search_content = search_content
    
    def run(self):
        try:
            conn = sqlite3.connect(DB_PATH)
            cursor = conn.cursor()
            
            if self.search_content:
                cursor.execute("SELECT path FROM files WHERE name LIKE ?", (f"%{self.query}%",))
                results = []
                for row in cursor.fetchall():
                    path = row[0]
                    try:
                        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                            content = f.read()
                            if self.query.lower() in content.lower():
                                results.append(path)
                    except:
                        pass
            else:
                cursor.execute("SELECT path FROM files WHERE name LIKE ?", (f"%{self.query}%",))
                results = [row[0] for row in cursor.fetchall()]
            
            conn.close()
            self.results_found.emit(results)
        except Exception as e:
            self.results_found.emit([])
        finally:
            self.finished.emit()

class ScanThread(QThread):
    progress = pyqtSignal(int, int)
    finished = pyqtSignal(int)
    
    def __init__(self, path):
        super().__init__()
        self.path = path
    
    def run(self):
        try:
            import subprocess
            result = subprocess.run(
                ['./cascade_search', '/scan', self.path],
                capture_output=True,
                text=True,
                cwd=os.path.dirname(os.path.abspath(__file__))
            )
            self.finished.emit(0 if result.returncode == 0 else 1)
        except Exception as e:
            self.finished.emit(1)

class CascadeSearchGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.search_thread = None
        self.scan_thread = None
        self.init_ui()
        self.check_database()
    
    def init_ui(self):
        self.setWindowTitle("CASCADE Search Tool - GUI")
        self.setGeometry(100, 100, 1200, 700)
        
        # Central widget
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        
        # Search section
        search_layout = QHBoxLayout()
        
        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText("Enter search query...")
        self.search_input.returnPressed.connect(self.perform_search)
        search_layout.addWidget(self.search_input)
        
        self.search_btn = QPushButton("Search")
        self.search_btn.clicked.connect(self.perform_search)
        search_layout.addWidget(self.search_btn)
        
        self.content_checkbox = QCheckBox("Search content")
        search_layout.addWidget(self.content_checkbox)
        
        layout.addLayout(search_layout)
        
        # Scan section
        scan_layout = QHBoxLayout()
        
        self.path_input = QLineEdit()
        self.path_input.setPlaceholderText("Directory to scan...")
        self.path_input.setText(os.path.expanduser("~/testbed"))
        scan_layout.addWidget(self.path_input)
        
        self.scan_btn = QPushButton("Scan Directory")
        self.scan_btn.clicked.connect(self.perform_scan)
        scan_layout.addWidget(self.scan_btn)
        
        layout.addLayout(scan_layout)
        
        # Progress bar
        self.progress_bar = QProgressBar()
        self.progress_bar.setVisible(False)
        layout.addWidget(self.progress_bar)
        
        # Results section
        results_label = QLabel("Search Results:")
        results_label.setStyleSheet("font-weight: bold; font-size: 14px;")
        layout.addWidget(results_label)
        
        self.results_tree = QTreeWidget()
        self.results_tree.setHeaderLabels(["File Path"])
        self.results_tree.setColumnWidth(0, 800)
        layout.addWidget(self.results_tree)
        
        # Status bar
        self.statusbar = QStatusBar()
        self.setStatusBar(self.statusbar)
        self.statusbar.showMessage("Ready")
    
    def check_database(self):
        if not os.path.exists(DB_PATH):
            self.statusbar.showMessage("Database not found. Run: ./cascade_search --setup")
            QMessageBox.warning(
                self,
                "Database Not Found",
                "Database not found. Please run: ./cascade_search --setup"
            )
        else:
            self.statusbar.showMessage(f"Database: {DB_PATH}")
    
    def perform_search(self):
        query = self.search_input.text().strip()
        if not query:
            QMessageBox.warning(self, "Empty Query", "Please enter a search query.")
            return
        
        if not os.path.exists(DB_PATH):
            QMessageBox.warning(self, "Database Not Found", "Please run: ./cascade_search --setup")
            return
        
        self.results_tree.clear()
        self.statusbar.showMessage(f"Searching for: {query}...")
        self.search_btn.setEnabled(False)
        
        self.search_thread = SearchThread(query, self.content_checkbox.isChecked())
        self.search_thread.results_found.connect(self.display_results)
        self.search_thread.finished.connect(self.search_finished)
        self.search_thread.start()
    
    def display_results(self, results):
        for path in results:
            item = QTreeWidgetItem([path])
            self.results_tree.addTopLevelItem(item)
        
        self.statusbar.showMessage(f"Found {len(results)} results")
    
    def search_finished(self):
        self.search_btn.setEnabled(True)
    
    def perform_scan(self):
        path = self.path_input.text().strip()
        if not path:
            QMessageBox.warning(self, "Empty Path", "Please enter a directory path.")
            return
        
        if not os.path.exists(path):
            QMessageBox.warning(self, "Invalid Path", "Directory does not exist.")
            return
        
        self.statusbar.showMessage(f"Scanning {path}...")
        self.scan_btn.setEnabled(False)
        self.progress_bar.setVisible(True)
        self.progress_bar.setRange(0, 0)  # Indeterminate progress
        
        self.scan_thread = ScanThread(path)
        self.scan_thread.finished.connect(self.scan_finished)
        self.scan_thread.start()
    
    def scan_finished(self, success):
        self.scan_btn.setEnabled(True)
        self.progress_bar.setVisible(False)
        
        if success == 0:
            self.statusbar.showMessage("Scan complete")
            QMessageBox.information(self, "Scan Complete", "Directory scanned successfully.")
        else:
            self.statusbar.showMessage("Scan failed")
            QMessageBox.warning(self, "Scan Failed", "Failed to scan directory.")

def main():
    app = QApplication(sys.argv)
    window = CascadeSearchGUI()
    window.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
