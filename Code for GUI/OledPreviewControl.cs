using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Windows.Forms;

namespace Jdy31OledMonitor
{
    // Visual mirror of the 8-line OLED content produced by the embedded firmware.
    public sealed class OledPreviewControl : Control
    {
        // The preview keeps the same 8-row mental model as the real display.
        private readonly string[] _lines = new string[8];

        public OledPreviewControl()
        {
            DoubleBuffered = true;
            ResizeRedraw = true;
            Size = new Size(320, 240);
            Font = new Font("Consolas", 10F, FontStyle.Bold, GraphicsUnit.Point);
            ForeColor = Color.FromArgb(160, 255, 255);
            BackColor = Color.FromArgb(8, 18, 32);

            for (int i = 0; i < _lines.Length; i++)
            {
                _lines[i] = string.Empty;
            }
        }

        // Update all rows at once so the control always redraws a coherent OLED frame.
        public void SetLines(params string[] lines)
        {
            for (int i = 0; i < _lines.Length; i++)
            {
                _lines[i] = i < lines.Length && lines[i] != null ? lines[i] : string.Empty;
            }

            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            // Draw a stylized device frame, a faint row grid, and then the current text content.
            base.OnPaint(e);

            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            e.Graphics.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;

            Rectangle outer = ClientRectangle;
            outer.Inflate(-2, -2);
            if (outer.Width <= 0 || outer.Height <= 0)
            {
                return;
            }

            using (GraphicsPath path = CreateRoundRect(outer, 24))
            using (SolidBrush surfaceBrush = new SolidBrush(BackColor))
            using (LinearGradientBrush borderBrush = new LinearGradientBrush(outer, Color.FromArgb(120, 247, 111, 218), Color.FromArgb(120, 93, 196, 255), LinearGradientMode.ForwardDiagonal))
            using (Pen borderPen = new Pen(borderBrush, 3f))
            {
                e.Graphics.FillPath(surfaceBrush, path);
                e.Graphics.DrawPath(borderPen, path);
            }

            int headerHeight = 24;
            Rectangle inner = new Rectangle(outer.X + 14, outer.Y + headerHeight + 10, outer.Width - 28, outer.Height - headerHeight - 24);
            if (inner.Width <= 0 || inner.Height <= 0)
            {
                return;
            }

            using (Pen gridPen = new Pen(Color.FromArgb(18, 45, 70), 1f))
            {
                int rowHeight = Math.Max(18, inner.Height / 8);
                for (int i = 1; i < 8; i++)
                {
                    int y = inner.Top + rowHeight * i;
                    e.Graphics.DrawLine(gridPen, inner.Left, y, inner.Right, y);
                }
            }

            float fontSize = CalculateBestFontSize(e.Graphics, inner);
            using (Font oledFont = new Font("Consolas", fontSize, FontStyle.Bold, GraphicsUnit.Point))
            {
                int rowHeight = Math.Max(18, inner.Height / 8);
                for (int i = 0; i < _lines.Length; i++)
                {
                    Rectangle textRect = new Rectangle(inner.Left + 4, inner.Top + i * rowHeight + 1, inner.Width - 8, rowHeight - 2);
                    TextRenderer.DrawText(
                        e.Graphics,
                        _lines[i] ?? string.Empty,
                        oledFont,
                        textRect,
                        ForeColor,
                        TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine);
                }
            }

            TextRenderer.DrawText(
                e.Graphics,
                "OLED Mirror",
                new Font("Segoe UI Semibold", 9F, FontStyle.Regular, GraphicsUnit.Point),
                new Rectangle(outer.Left + 14, outer.Top + 5, outer.Width - 28, headerHeight),
                Color.FromArgb(255, 216, 235),
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine);
        }

        // Pick the largest monospaced font that still fits the longest line into the available row width.
        private float CalculateBestFontSize(Graphics graphics, Rectangle inner)
        {
            int rowHeight = Math.Max(18, inner.Height / 8);
            int longest = 0;
            for (int i = 0; i < _lines.Length; i++)
            {
                string line = _lines[i] ?? string.Empty;
                if (line.Length > longest)
                {
                    longest = line.Length;
                }
            }

            if (longest <= 0)
            {
                longest = 8;
            }

            string sample = new string('W', Math.Min(21, longest));
            for (float size = 13.5f; size >= 7f; size -= 0.5f)
            {
                using (Font font = new Font("Consolas", size, FontStyle.Bold, GraphicsUnit.Point))
                {
                    Size proposed = new Size(5000, 5000);
                    Size measured = TextRenderer.MeasureText(graphics, sample, font, proposed, TextFormatFlags.NoPadding | TextFormatFlags.SingleLine);
                    if (measured.Width <= inner.Width - 6 && measured.Height <= rowHeight - 2)
                    {
                        return size;
                    }
                }
            }

            return 7f;
        }

        private static GraphicsPath CreateRoundRect(Rectangle bounds, int radius)
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
