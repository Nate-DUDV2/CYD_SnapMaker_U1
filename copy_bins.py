Import("env")
import shutil
import os
import time

# Get the current build environment name (e.g., cyd_28, cyd_28_dual_usb)
env_name = env.get("PIOENV")

# Set up the source and destination paths
build_dir = env.subst("$BUILD_DIR")
target_dir = os.path.join(env.subst("$PROJECT_DIR"), "web-flasher")

# Ensure the web-flasher folder exists
os.makedirs(target_dir, exist_ok=True)

# Clean up the suffix for the file names
suffix = env_name.replace("cyd_", "")

def copy_binaries(*args, **kwargs):
    print(f"\n---> AUTOMATION: Copying {env_name} binaries to web-flasher folder...")
    
    # Give Windows a half-second to finish physically writing the .bin file to the disk
    time.sleep(0.5)
    
    # We know exactly what PlatformIO names these files natively
    files_to_copy = {
        "firmware.bin": f"firmware_{suffix}.bin",
        "partitions.bin": f"partitions_{suffix}.bin",
        "bootloader.bin": f"bootloader_{suffix}.bin"
    }
    
    for src_name, dst_name in files_to_copy.items():
        src_path = os.path.join(build_dir, src_name)
        dst_path = os.path.join(target_dir, dst_name)
        
        if os.path.exists(src_path):
            shutil.copy(src_path, dst_path)
            print(f"  [+] Saved: {dst_name}")
        else:
            print(f"  [X] ERROR: Could not find {src_name}")
            
    print("---> DONE!\n")

# Hook explicitly onto the .bin file generation
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_binaries)