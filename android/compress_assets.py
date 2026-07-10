import os
import sys
from PIL import Image
from concurrent.futures import ProcessPoolExecutor

# Target maximum texture size for mobile compression
MAX_SIZE = 256

def compress_image(file_path):
    try:
        if not os.path.exists(file_path):
            return 0
        
        # Check file extension
        ext = os.path.splitext(file_path)[1].lower()
        if ext not in ['.png', '.jpg', '.jpeg']:
            return 0

        old_size = os.path.getsize(file_path)
        with Image.open(file_path) as img:
            # Determine if resize is needed
            w, h = img.size
            if w > MAX_SIZE or h > MAX_SIZE:
                if w > h:
                    new_w = MAX_SIZE
                    new_h = int(MAX_SIZE * h / w)
                else:
                    new_h = MAX_SIZE
                    new_w = int(MAX_SIZE * w / h)
                
                # Use LANCZOS (equivalent to ANTIALIAS in older PIL versions)
                img = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
            
            # Save optimized version in place
            if ext in ['.jpg', '.jpeg']:
                img.save(file_path, 'JPEG', quality=75, optimize=True)
            elif ext == '.png':
                # Save with maximum PNG compression
                img.save(file_path, 'PNG', optimize=True, compress_level=9)
                
        new_size = os.path.getsize(file_path)
        return old_size - new_size
    except Exception as e:
        return 0

def main():
    assets_dir = os.path.join(os.path.dirname(__file__), 'assets', 'data')
    if not os.path.exists(assets_dir):
        print(f"Error: Assets directory {assets_dir} does not exist.")
        sys.exit(1)
        
    print(f"Scanning textures in {assets_dir}...")
    image_paths = []
    for root, dirs, files in os.walk(assets_dir):
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in ['.png', '.jpg', '.jpeg']:
                image_paths.append(os.path.join(root, f))
                
    total_images = len(image_paths)
    print(f"Found {total_images} images to compress. Starting parallel processing...")
    
    saved_bytes = 0
    # Use ProcessPoolExecutor to compress images in parallel across all CPU cores
    with ProcessPoolExecutor() as executor:
        results = executor.map(compress_image, image_paths)
        for idx, saved in enumerate(results):
            saved_bytes += saved
            if (idx + 1) % 500 == 0 or idx + 1 == total_images:
                print(f"Processed {idx + 1}/{total_images} images...")
                
    saved_mb = saved_bytes / (1024 * 1024)
    print(f"Compression completed. Total saved space: {saved_mb:.2f} MB")

if __name__ == '__main__':
    main()
