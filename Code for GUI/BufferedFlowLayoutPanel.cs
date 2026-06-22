using System.Windows.Forms;

namespace Jdy31OledMonitor
{
    // FlowLayoutPanel variant with double buffering enabled for smoother resize and animation behavior.
    public class BufferedFlowLayoutPanel : FlowLayoutPanel
    {
        public BufferedFlowLayoutPanel()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            DoubleBuffered = true;
            ResizeRedraw = true;
        }
    }
}
