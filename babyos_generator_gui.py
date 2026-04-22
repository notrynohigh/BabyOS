#!/usr/bin/env python3
"""
BabyOS Project Generator - Interactive GUI Version
A Windows-friendly interface for generating BabyOS projects

Requirements:
    pip install tkinter  # Usually comes with Python on Windows

On Windows, just double-click to run.
On Linux/macOS, run: python3 babyos_generator_gui.py
"""

import os
import sys
import subprocess
from pathlib import Path

# Try to import tkinter - most common on Windows
try:
    import tkinter as tk
    from tkinter import ttk, messagebox, filedialog
    HAS_TKINTER = True
except ImportError:
    HAS_TKINTER = False

# Import the core generator
from babyos_project_generator import (
    BabyOSProjectGenerator, get_bos_root, check_bos_directory,
    VENDORS, VENDOR_CHIPS, VERSION
)

class BabyOSGeneratorGUI:
    """GUI for BabyOS Project Generator."""

    def __init__(self, root):
        self.root = root
        self.root.title(f"BabyOS Project Generator v{VERSION}")
        self.root.geometry("700x550")
        self.root.resizable(True, True)

        # Variables
        self.bos_root_var = tk.StringVar()
        self.vendor_var = tk.StringVar()
        self.chip_var = tk.StringVar()
        self.project_name_var = tk.StringVar()
        self.output_dir_var = tk.StringVar()

        # Auto-detect BabyOS
        detected_bos = get_bos_root()
        if detected_bos:
            self.bos_root_var.set(detected_bos)

        self.setup_ui()

    def setup_ui(self):
        """Setup the UI components."""
        # Main frame
        main_frame = ttk.Frame(self.root, padding="20")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Title
        title_label = ttk.Label(main_frame, text="BabyOS Project Generator",
                                font=("Arial", 16, "bold"))
        title_label.pack(pady=(0, 20))

        # BabyOS Path
        path_frame = ttk.LabelFrame(main_frame, text="BabyOS Directory", padding="10")
        path_frame.pack(fill=tk.X, pady=(0, 15))

        ttk.Label(path_frame, text="Path:").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(path_frame, textvariable=self.bos_root_var, width=50).grid(row=0, column=1, padx=5)
        ttk.Button(path_frame, text="Browse", command=self.browse_bos).grid(row=0, column=2)

        # Vendor selection
        vendor_frame = ttk.LabelFrame(main_frame, text="MCU Vendor", padding="10")
        vendor_frame.pack(fill=tk.X, pady=(0, 15))

        ttk.Label(vendor_frame, text="Vendor:").grid(row=0, column=0, sticky=tk.W)
        self.vendor_combo = ttk.Combobox(vendor_frame, textvariable=self.vendor_var,
                                         values=list(VENDORS.keys()), state="readonly", width=20)
        self.vendor_combo.grid(row=0, column=1, padx=5)
        self.vendor_combo.bind("<<ComboboxSelected>>", self.on_vendor_selected)

        ttk.Label(vendor_frame, text="Chip:").grid(row=0, column=2, padx=(20, 5))
        self.chip_combo = ttk.Combobox(vendor_frame, textvariable=self.chip_var, width=20)
        self.chip_combo.grid(row=0, column=3, padx=5)

        # Project settings
        project_frame = ttk.LabelFrame(main_frame, text="Project Settings", padding="10")
        project_frame.pack(fill=tk.X, pady=(0, 15))

        ttk.Label(project_frame, text="Project Name:").grid(row=0, column=0, sticky=tk.W)
        name_entry = ttk.Entry(project_frame, textvariable=self.project_name_var, width=35)
        name_entry.grid(row=0, column=1, padx=5, columnspan=2, sticky=tk.W)

        ttk.Label(project_frame, text="Output Directory:").grid(row=1, column=0, sticky=tk.W, pady=(10, 0))
        ttk.Entry(project_frame, textvariable=self.output_dir_var, width=35).grid(row=1, column=1, padx=5, columnspan=2, sticky=tk.W, pady=(10, 0))
        ttk.Button(project_frame, text="Browse", command=self.browse_output).grid(row=1, column=3, padx=5, pady=(10, 0))

        # Info text
        info_frame = ttk.LabelFrame(main_frame, text="Information", padding="10")
        info_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 15))

        self.info_text = tk.Text(info_frame, height=8, wrap=tk.WORD)
        self.info_text.pack(fill=tk.BOTH, expand=True)
        scrollbar = ttk.Scrollbar(info_frame, command=self.info_text.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.info_text.config(yscrollcommand=scrollbar.set)

        self.update_info()

        # Buttons
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill=tk.X)

        ttk.Button(button_frame, text="Generate Project", command=self.generate_project).pack(side=tk.LEFT, padx=(0, 10))
        ttk.Button(button_frame, text="Clear", command=self.clear_form).pack(side=tk.LEFT)
        ttk.Button(button_frame, text="List Vendors", command=self.list_vendors).pack(side=tk.RIGHT)

    def browse_bos(self):
        """Browse for BabyOS directory."""
        directory = filedialog.askdirectory(title="Select BabyOS Directory")
        if directory:
            self.bos_root_var.set(directory)
            self.update_info()

    def browse_output(self):
        """Browse for output directory."""
        directory = filedialog.askdirectory(title="Select Output Directory")
        if directory:
            self.output_dir_var.set(directory)

    def on_vendor_selected(self, event):
        """Handle vendor selection."""
        vendor = self.vendor_var.get()
        if vendor in VENDOR_CHIPS:
            self.chip_combo["values"] = VENDOR_CHIPS[vendor]
            self.chip_var.set("")
            # Auto-generate project name
            self.project_name_var.set(f"babyos_{vendor}")

    def list_vendors(self):
        """List all vendors in info."""
        self.update_info()

    def update_info(self):
        """Update info text."""
        info = f"""BabyOS Project Generator v{VERSION}
=====================================

Supported MCU Vendors: {len(VENDORS)}
  {', '.join(VENDORS.keys())}

Supported Chips per Vendor:
  st:      {len(VENDOR_CHIPS.get('st', []))} chips
  nation:  {len(VENDOR_CHIPS.get('nation', []))} chips
  espressif: {len(VENDOR_CHIPS.get('espressif', []))} chips

Current BabyOS Path:
  {self.bos_root_var.get() or 'Not set'}

Usage:
  1. Select or browse to BabyOS directory
  2. Choose MCU vendor and chip
  3. Set project name and output directory
  4. Click "Generate Project"

The generator will create:
  - Makefile (build system)
  - Linker script (chip-specific)
  - Startup code
  - Template main.c
  - Initial b_config.h
  - README.md
"""
        self.info_text.delete(1.0, tk.END)
        self.info_text.insert(1.0, info)

    def clear_form(self):
        """Clear all form fields."""
        self.vendor_var.set("")
        self.chip_var.set("")
        self.project_name_var.set("")
        self.chip_combo["values"] = []

    def generate_project(self):
        """Generate the project."""
        # Validate inputs
        bos_root = self.bos_root_var.get().strip()
        vendor = self.vendor_var.get().strip()
        chip = self.chip_var.get().strip()
        output_dir = self.output_dir_var.get().strip()
        project_name = self.project_name_var.get().strip()

        if not bos_root:
            messagebox.showerror("Error", "Please select BabyOS directory")
            return

        if not vendor or not chip:
            messagebox.showerror("Error", "Please select vendor and chip")
            return

        if not output_dir:
            output_dir = "./babyos_project"

        if not project_name:
            project_name = f"babyos_{vendor}_{chip.lower()}"

        # Validate BabyOS
        valid, msg = check_bos_directory(bos_root)
        if not valid:
            messagebox.showerror("Error", f"Invalid BabyOS directory:\n{msg}")
            return

        # Generate project
        try:
            generator = BabyOSProjectGenerator(
                bos_root=bos_root,
                output_dir=Path(output_dir),
                vendor=vendor,
                chip=chip,
                project_name=project_name
            )
            generator.generate()

            messagebox.showinfo("Success",
                f"Project generated successfully!\n\n"
                f"Location: {output_dir}\n\n"
                f"Next steps:\n"
                f"1. cd {output_dir}\n"
                f"2. Create 'bos' symlink to BabyOS on Windows\n"
                f"3. make config (optional)\n"
                f"4. make")

        except Exception as e:
            messagebox.showerror("Error", f"Failed to generate project:\n{str(e)}")

def main():
    if not HAS_TKINTER:
        print("Error: tkinter not available.")
        print("On Windows, tkinter usually comes with Python.")
        print("On Linux: sudo apt install python3-tk")
        print("\nFalling back to command-line mode...")
        print("Use: python babyos_project_generator.py --help")
        return 1

    root = tk.Tk()
    app = BabyOSGeneratorGUI(root)
    root.mainloop()
    return 0

if __name__ == "__main__":
    sys.exit(main())