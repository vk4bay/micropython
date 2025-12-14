# display_writer.py - Adapter for Writer to work with ili9488
class DisplayWriter:
    """Adapter to make ili9488 compatible with Writer class"""
    def __init__(self, display):
        self.display = display
        self.width = display.WIDTH
        self.height = display.HEIGHT
        
    def pixel(self, x, y, color=1):
        """Set a pixel - Writer expects this method"""
        # FIXED: Always draw pixel - don't check if color is truthy
        # because 0x000000 (BLACK) is falsy but valid
        # DEBUG: Uncomment next line to see what's being drawn
        # print(f"DisplayWriter.pixel({x}, {y}, 0x{color:06X})")
        self.display.pixel(x, y, color)
            
    def fill_rect(self, x, y, w, h, color):
        """Fill rectangle - Writer may use this for efficiency"""
        self.display.fill_rect(x, y, w, h, color)
        
    def show(self):
        """Update display"""
        self.display.show()


class Writer:
    """
    Simplified Writer class for rendering bitmap fonts
    Based on Peter Hinch's writer but adapted for custom displays
    """
    text_row = 0
    text_col = 0
    
    def __init__(self, device, font, color=0xFFFF, bg_color=0x0000):
        self.device = device
        self.font = font
        self.color = color
        self.bg_color = bg_color
        
        # Font metrics
        self.height = font.height()
        self.baseline = font.baseline() if hasattr(font, 'baseline') else self.height
        
    def _get_char(self, char):
        """Get character bitmap from font"""
        return self.font.get_ch(char)
        
    def set_textpos(self, row, col):
        """Set text cursor position in pixels"""
        Writer.text_row = row
        Writer.text_col = col
        
    def printstring(self, text, invert=False):
        """Print string at current cursor position"""
        for char in text:
            self._printchar(char, invert)
            
    def _printchar(self, char, invert=False):
        """Print single character"""
        if char == '\n':
            Writer.text_row += self.height + 2
            Writer.text_col = 0
            return
            
        # Get character data
        glyph, char_height, char_width = self._get_char(char)
        if glyph is None:
            return
        
        # FIXED: font_to_py generates fonts with each row padded to byte boundary
        bytes_per_row = (char_width + 7) // 8
        
        # Draw character
        for row in range(char_height):
            for col in range(char_width):
                # Each row starts on a byte boundary
                byte_in_row = col // 8
                bit_in_byte = 7 - (col % 8)  # MSB first within each byte
                byte_index = row * bytes_per_row + byte_in_row
                
                if byte_index < len(glyph):
                    pixel_on = (glyph[byte_index] >> bit_in_byte) & 1
                    x = Writer.text_col + col
                    y = Writer.text_row + row
                    
                    if invert:
                        pixel_on = not pixel_on
                    
                    # Determine which color to use
                    color = self.color if pixel_on else self.bg_color
                    
                    # Draw the pixel
                    # Always draw foreground (text) pixels, even if black
                    # Only skip background pixels if bg_color is transparent (0x0000)
                    if pixel_on:
                        # Always draw text pixels, even if color is black (0x000000)
                        self.device.pixel(x, y, color)
                    elif self.bg_color != 0x0000:
                        # Draw background pixel if it has a non-transparent color
                        self.device.pixel(x, y, color)
                    # If bg_color is 0x0000 and pixel is off, skip (transparent background)
        
        # Advance cursor
        Writer.text_col += char_width + 1  # +1 for spacing
        
    def stringlen(self, text):
        """Calculate pixel width of string"""
        width = 0
        for char in text:
            glyph, char_height, char_width = self._get_char(char)
            if glyph:
                width += char_width + 1
        return width - 1 if width > 0 else 0

