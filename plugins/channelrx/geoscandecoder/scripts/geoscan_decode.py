import sys
import wave
import numpy as np

def pn9(data):
    """
    PN9 XOR-scrambler (descrambler) for Geoscan payload.
    It matches the C++ derandomizePN9 implementation.
    """
    st = 0x1FF
    result = bytearray()
    for byte in data:
        mask = 0
        for b in range(7, -1, -1):
            mask |= ((st & 1) << b)
            st = ((st >> 1) | ((((st >> 8) ^ (st >> 4)) & 1) << 8)) & 0x1FF
        result.append(byte ^ mask)
    return bytes(result)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 geoscan_decode.py <input.wav>")
        sys.exit(1)
        
    wav_path = sys.argv[1]
    print(f"Loading WAV file: {wav_path}")
    
    with wave.open(wav_path, 'rb') as f:
        fs = f.getframerate()
        nframes = f.getnframes()
        channels = f.getnchannels()
        sampwidth = f.getsampwidth()
        
        if channels != 2 or sampwidth != 2:
            print("Expected 16-bit stereo IQ WAV file.")
            sys.exit(1)
            
        raw_data = f.readframes(nframes)
        
    print(f"Sample rate: {fs} Hz, frames: {nframes}")
    
    # Parse interleaved 16-bit stereo (I and Q)
    data = np.frombuffer(raw_data, dtype=np.int16).reshape(-1, 2)
    iq = data[:, 0].astype(np.float32) + 1j * data[:, 1].astype(np.float32)
    
    print("Demodulating GFSK...")
    # FM demodulation (instantaneous frequency)
    demod = np.angle(iq[1:] * np.conj(iq[:-1]))
    
    baud = 9600
    sps = fs / baud
    
    # Simple moving average low-pass filter
    ma_size = int(sps)
    kernel = np.ones(ma_size) / ma_size
    demod_filt = np.convolve(demod, kernel, mode='same')
    
    print(f"Baud rate: {baud}, Samples per symbol (SPS): {sps:.2f}")
    
    # Sync word and its bipolar representation for correlation
    sync_word = 0x930B51DE
    sync_bits = np.array([(sync_word >> (31 - i)) & 1 for i in range(32)], dtype=np.int8)
    sync_bits_bipolar = sync_bits * 2 - 1
    
    # Determine the optimal sampling phase offset
    print("Recovering clock and bits...")
    best_syncs = 0
    best_bits = None
    
    # Try different offsets to maximize matched sync words
    step = max(1, int(sps / 10))
    for offset in range(0, int(sps), step):
        idx = np.arange(len(demod_filt) - offset)
        symbol_indices = (idx * baud / fs).astype(np.int32)
        # Sum demodulated values inside each symbol period
        symbol_sums = np.bincount(symbol_indices, weights=demod_filt[offset:])
        
        # Hard decision
        bits = (symbol_sums > 0).astype(np.int8)
        bits_bipolar = bits * 2 - 1
        
        # Cross-correlate to find sync words
        corr = np.correlate(bits_bipolar, sync_bits_bipolar, mode='valid')
        num_syncs = np.sum(corr >= 30) # Allow up to 1 bit error in sync word
        
        if num_syncs > best_syncs:
            best_syncs = num_syncs
            best_bits = bits
            
    if best_syncs == 0:
        print("No valid frames found.")
        sys.exit(0)
        
    print(f"Found {best_syncs} frames with optimal phase.")
    
    # Locate exact positions of the sync words using the best phase
    bits_bipolar = best_bits * 2 - 1
    corr = np.correlate(bits_bipolar, sync_bits_bipolar, mode='valid')
    
    # We look for high correlation (e.g. >= 30 out of 32)
    sync_positions = np.where(corr >= 30)[0]
    
    print(f"Sync word positions (in bits): {sync_positions}")
    print("\n--- Decoded Frames ---")
    
    # Extract frames
    frame_len_bytes = 70
    frame_len_bits = frame_len_bytes * 8
    
    for i, pos in enumerate(sync_positions):
        # Payload starts immediately after the 32-bit sync word
        payload_start = pos + 32
        payload_end = payload_start + frame_len_bits
        
        if payload_end > len(best_bits):
            break
            
        payload_bits = best_bits[payload_start:payload_end]
        payload_bytes = np.packbits(payload_bits).tobytes()
        
        # Descramble using PN9
        descrambled = pn9(payload_bytes)
        
        print(f"Frame {i:02d} (Bit index {pos}):")
        print("  Raw hex :", payload_bytes.hex())
        print("  Decoded :", descrambled.hex())
        # Make ASCII representation safely printable
        ascii_rep = ''.join(chr(b) if 32 <= b < 127 else '.' for b in descrambled)
        print("  ASCII   :", ascii_rep)
        print("-" * 40)

if __name__ == '__main__':
    main()
