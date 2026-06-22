using System.Windows.Forms;

namespace Jdy31OledMonitor
{
    // Lightweight panel used throughout the dashboard to reduce flicker while controls are redrawn.
    public class BufferedPanel : Panel
    {
        public BufferedPanel()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            DoubleBuffered = true;
            ResizeRedraw = true;
        }
    }
}
