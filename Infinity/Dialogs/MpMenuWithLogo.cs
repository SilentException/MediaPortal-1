using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Windows;
using System.Windows.Markup;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using ProjectInfinity;
using ProjectInfinity.Logging;
using ProjectInfinity.Navigation;
using ProjectInfinity.Controls;

namespace Dialogs
{
  /// <summary>
  /// Interaction logic for UserControl1.xaml
  /// </summary>

  public partial class MpMenuWithLogo : ViewWindow
  {
    DialogViewModel _model;
    /// <summary>
    /// Initializes a new instance of the <see cref="MpImageMenu"/> class.
    /// </summary>
    public MpMenuWithLogo()
    {
      this.Visibility = Visibility.Visible;
      this.BorderThickness = new Thickness(0);
      this.Width = 530;
      this.Height = 370;

      _model = new DialogViewModel(this);
      _model.SetItems(new DialogMenuItemCollection());
      Size scaling = ServiceScope.Get<INavigationService>().CurrentScaling;
      this.Width *= scaling.Width;
      this.Height *= scaling.Height;
      DataContext = _model;
      this.InputBindings.Add(new KeyBinding(_model.Close, new KeyGesture(System.Windows.Input.Key.Escape)));
    }
    public MpMenuWithLogo(DialogMenuItemCollection items)
    {
      this.Visibility = Visibility.Visible;
      this.BorderThickness = new Thickness(0);
      this.Width = 530;
      this.Height = 370;

      _model = new DialogViewModel(this);
      _model.SetItems(items);
      Size scaling = ServiceScope.Get<INavigationService>().CurrentScaling;
      this.Width *= scaling.Width;
      this.Height *= scaling.Height;
      DataContext = _model;
      this.InputBindings.Add(new KeyBinding(_model.Close, new KeyGesture(System.Windows.Input.Key.Escape)));
    }

    protected override void OnRenderSizeChanged(SizeChangedInfo sizeInfo)
    {
      base.OnRenderSizeChanged(sizeInfo);
      if (base.Content != null)
      {
        Size scaling = ServiceScope.Get<INavigationService>().CurrentScaling;
        ((FrameworkElement)base.Content).LayoutTransform = new ScaleTransform(scaling.Width, scaling.Height);
      }
    }
    protected override void OnContentChanged(object oldContent, object newContent)
    {
      base.OnContentChanged(oldContent, newContent);
      Size scaling = ServiceScope.Get<INavigationService>().CurrentScaling;
      ((FrameworkElement)base.Content).LayoutTransform = new ScaleTransform(scaling.Width, scaling.Height);
    }

    public string SubTitle
    {
      get
      {
        return _model.Title;
      }
      set
      {
        _model.Title = value;
      }
    }

    public string Header
    {
      get
      {
        return _model.Header;
      }
      set
      {
        _model.Header = value;
      }
    }
    /// <summary>
    /// Gets or sets the index of the selected.
    /// </summary>
    /// <value>The index of the selected.</value>
    public int SelectedIndex
    {
      get
      {
        return _model.SelectedIndex;
      }
      set
      {
        _model.SetSelectedIndex(value);
      }
    }

    /// <summary>
    /// Gets or sets the selected item.
    /// </summary>
    /// <value>The selected item.</value>
    public DialogMenuItem SelectedItem
    {
      get
      {
        if (SelectedIndex < 0) return null;
        return _model.Items[SelectedIndex];
      }
    }

    /// <summary>
    /// Gets or sets the items.
    /// </summary>
    /// <value>The items.</value>
    public DialogMenuItemCollection Items
    {
      get
      {
        return _model.Items;
      }
      set
      {
        _model.Items = value;
      }
    }
  }
}