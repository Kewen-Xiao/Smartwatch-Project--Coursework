using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace Jdy31OledMonitor
{
    // Card-like container used to keep the dashboard visuals consistent.
    public sealed class RoundedPanel : BufferedPanel
    {
        private int _cornerRadius = 26;

        public Color FillColor { get; set; }
        public Color BorderColor { get; set; }

        public int CornerRadius
        {
            get { return _cornerRadius; }
            set
            {
                _cornerRadius = Math.Max(2, value);
                UpdateRoundedRegion();
                Invalidate();
            }
        }

        public RoundedPanel()
        {
            BackColor = Color.Transparent;
            FillColor = Color.FromArgb(236, 255, 255, 255);
            BorderColor = Color.FromArgb(198, 214, 228, 246);
            CornerRadius = 26;
            Margin = new Padding(0);
        }

        protected override void OnSizeChanged(EventArgs e)
        {
            base.OnSizeChanged(e);
            UpdateRoundedRegion();
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            base.OnPaintBackground(e);

            Rectangle rect = ClientRectangle;
            rect.Width -= 1;
            rect.Height -= 1;
            if (rect.Width <= 0 || rect.Height <= 0)
            {
                return;
            }

            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using (GraphicsPath path = CreateRoundedRectangle(rect, CornerRadius))
            using (SolidBrush fill = new SolidBrush(FillColor))
            using (Pen pen = new Pen(BorderColor))
            {
                e.Graphics.FillPath(fill, path);
                e.Graphics.DrawPath(pen, path);
            }
        }

        // Keep the WinForms region in sync with the painted rounded outline so hit-testing matches the visual shape.
        private void UpdateRoundedRegion()
        {
            Rectangle rect = ClientRectangle;
            if (rect.Width <= 0 || rect.Height <= 0)
            {
                return;
            }

            using (GraphicsPath path = CreateRoundedRectangle(rect, CornerRadius))
            {
                Region = new Region(path);
            }
        }

        private static GraphicsPath CreateRoundedRectangle(Rectangle bounds, int radius)
        {
            int diameter = Math.Max(2, radius * 2);
            GraphicsPath path = new GraphicsPath();
            path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180, 90);
            path.AddArc(bounds.Right - diameter, bounds.Y, diameter, diameter, 270, 90);
            path.AddArc(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter, 0, 90);
            path.AddArc(bounds.X, bounds.Bottom - diameter, diameter, diameter, 90, 90);
            path.CloseFigure();
            return path;
        }
    }
}
