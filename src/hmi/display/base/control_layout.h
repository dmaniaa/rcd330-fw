// File name is ours; stock name unknown.
//
// Portable, host-testable halves of the stock control library
// (STDPlusLogicalControlLibrary) and ControlCreator (display/base/
// controlcreator.cpp): scene-node lookup and element rects, the
// CompositeItem part layout, the IgdtButton node structure CreateButton
// parses, and the IgdtButtonBase touch state machine. No Qt, no GL, no
// locking: the Qt control classes call these and apply the results through
// hmi_wrapper_igdt_*; IgdtApplyCompositeSize applies them to an IgdtRuntime
// directly (host tests).
#ifndef RCD330_HMI_DISPLAY_BASE_CONTROL_LAYOUT_H
#define RCD330_HMI_DISPLAY_BASE_CONTROL_LAYOUT_H

#include <stdint.h>

#include "display/igdt/igdt_runtime.h"
#include "display/igdt/igdt_tables.h"

// GFX_MGR02_NUM_OF_SCREENS: GetElementRoot accepts screen ids below it.
enum { IGDT_CONTROL_SCREEN_LIMIT = 0xff };

// ControlCreator::GetElementRoot: the node tagged `tag` in the scene of
// screen_id (depth first over its root nodes), NULL when screen_id >= 0xff,
// the screen is unknown or no node has that tag.
const IgdtNode* IgdtFindNodeByTag(const IgdtSkinTables& t, uint8_t screen_id, uint32_t tag);

// QRect-style edges (inclusive right/bottom) as ControlCreator::
// GetElementRect returns them. The null rect is (0, 0, -1, -1).
struct IgdtElementRect {
    int left, top, right, bottom;
};

// ControlCreator::GetElementRect: image -> record x/y with the texture size;
// text -> layout-aligned x, record y, layout width and (stock quirk)
// bottom = top - 1; a clipping group -> its clip_table rect; any other group,
// branch or state -> the union of all children's valid rects (all children,
// not just the selected one); anything else or NULL -> the null rect.
IgdtElementRect IgdtGetElementRect(const IgdtSkinTables& t, const IgdtNode* node);

// IgdtGetElementRect as x, y, w, h (w = right - left + 1). False, with the
// outputs untouched, for NULL or a null rect.
bool IgdtCalcNodeRect(const IgdtSkinTables& t, const IgdtNode* node, int* x, int* y, int* w, int* h);

// CompositeItem::ITEM_COMBINATION_FORM (CompositeItem::CompositeItem
// asserts the part count: 1, 3, 3, 9).
enum CompositeForm {
    ICF_ONE_PART = 0,   // the single part takes the whole size
    ICF_LCR = 1,        // left, stretched centre, right
    ICF_UML = 2,        // upper, stretched middle, lower
    ICF_9_PARTS = 3     // UL UC UR / ML MC MR / LL LC LR
};

// Number of parts `form` takes, 0 for an unknown form.
int CompositePartCount(int form);

// One part as CompositeItem sees it: position relative to the composite
// (VMoveableBase relX/relY), current size (ItemBase +0x20/+0x24), and
// whether the layout moved it (stock calls SetX/SetY only on some parts).
struct CompositePart {
    int rel_x, rel_y;
    int w, h;
    bool x_set, y_set;
};

// The layout half of CompositeItem::SetSize: updates parts[] for a
// composite of size w x h. False (nothing changed) when count does not match
// the form.
bool ComposeCompositeParts(int form, int w, int h, CompositePart* parts, int count);

// One image part as ControlCreator::CreateImage builds it from a node: the
// IGDT tags it scales and moves, the rect of the node it stopped at, its
// move id (the nearest dynamic group/branch above it, or 0) and, for a state
// or branch node, its dynamic id (node eidx | kind).
enum { IGDT_IMAGE_MAX_TAGS = 16 };
struct IgdtImagePart {
    uint32_t tags[IGDT_IMAGE_MAX_TAGS];
    int tag_count;
    IgdtElementRect rect;
    uint32_t move_id;
    uint32_t dyn_id;
};

// CreateImage's walk: descends groups / branches through their first child
// (picking up the first dynamic tag as move id), stops at an image (one tag)
// or a state (one tag per child). False for anything else.
bool IgdtResolveImagePart(const IgdtSkinTables& t, const IgdtNode* node, uint32_t move_id, IgdtImagePart* out);

// CreateComposite's part list: every child of a group root that resolves
// to an image part, or the root itself when it is an image or state node.
// Returns the number written (at most max).
enum { IGDT_COMPOSITE_MAX_PARTS = 9 };
int IgdtCollectCompositeParts(const IgdtSkinTables& t, const IgdtNode* root, IgdtImagePart* out, int max);

// The stock dynamic id of a node (0x002d6590): a dynamic tag becomes
// eidx | kind, a static tag is returned as is, 0 for NULL.
uint32_t IgdtNodeDynId(const IgdtNode* node);

// Builds the composite of `root` at (x, y, w, h) as CreateComposite +
// CompositeItem::SetSize do and writes the result into rt: scale of every
// part tag (size / original size), x/y of the parts the layout moves (the
// part's move-id group by its displacement when it has one, else its tags
// to their new absolute position). The composite's own group is not moved:
// its origin is (x, y), so its displacement is 0. False when root is NULL,
// the part count does not match the form, or an IGDT setter failed.
bool IgdtApplyCompositeSize(const IgdtSkinTables& t, IgdtRuntime& rt, const IgdtNode* root, int x, int y, int w,
                            int h, int slice_mode);

// The nodes ControlCreator::CreateButton reads from a button root: the root
// itself is the content group unless its first child is a branch (the
// visibility branch; the content is its first child that has children).
// content[0] is the touch group, content[1] the background (a composite, or
// a branch of composites). With more than two parts, content[2] is either
// the state branch/state (dyn id; the text root is then content[3] when
// there are four or more parts) or, when it is neither, the text root.
struct IgdtButtonNodes {
    uint32_t visibility_id;       // branch above the content (0x01000044 on VBtnInfo), or 0
    uint32_t touch_id;            // content[0] tag (touch path terminal)
    uint32_t dyn_id;              // content[2] dyn id when a branch/state, else 0
    const IgdtNode* content;      // the group holding the parts
    const IgdtNode* background;   // content[1]
    bool background_is_branch;    // content[1] is a branch of backgrounds
    const IgdtNode* text_root;    // node CreateButton builds text items from, or NULL
};

// The node walk of CreateButton (0x002dd4e0). False when the root has no
// usable content (CreateButton returns NULL then).
bool IgdtParseButtonNodes(const IgdtSkinTables& t, const IgdtNode* root, IgdtButtonNodes* out);

// One text item as ControlCreator::CreateText builds it: the rect, the
// runtime text id (= text_layout index = node eidx), the layout's font, the
// three glow image tags of a composite text (0 otherwise), the text's own
// dynamic tag (0 when static) and the move id.
struct IgdtTextPart {
    IgdtElementRect rect;
    unsigned text_id;
    uint8_t font;
    uint32_t glow[3];
    uint32_t dyn_tag;
    uint32_t move_id;
};

// CreateText's walk: a text node directly; a group/branch with one child
// descends (the first dynamic tag becomes the move id); a group with two or
// more children whose first child has three or more is a composite text
// (glow images, then the text). False for anything else.
bool IgdtResolveTextPart(const IgdtSkinTables& t, const IgdtNode* node, uint32_t move_id, IgdtTextPart* out);

// The text items CreateButton builds from nodes.text_root. Returns the
// number written (at most max).
int IgdtCollectButtonTexts(const IgdtSkinTables& t, const IgdtButtonNodes& nodes, IgdtTextPart* out, int max);

// IgdtButton states (ButtonBase +0x44). Stock SetState only uses 0..2;
// selection is the separate ButtonBase +0x48 flag. BTN_SELECTED is kept for
// the phase 2 plan and never produced here.
enum ButtonState { BTN_NORMAL = 0, BTN_PRESSED = 1, BTN_DISABLED = 2, BTN_SELECTED = 3 };

struct ButtonClickResult {
    bool state_changed;   // SetState was called (stock re-applies even an equal state)
    int new_state;
    bool clicked;         // SigClicked would be emitted
};

// IgdtButtonBase::TouchEventHandler (0x002ea27c) with ButtonBase::OnLeave /
// OnPress / OnRelease / OnClicked, for a button not inside its double-click
// block: a cancel-outside flag first leaves (-> normal), then the low byte
// dispatches: 1 press (-> pressed), 3 release (-> normal, and a click unless
// the event is 0x80000003 or carries 0x40000000). A disabled button changes
// nothing and never clicks.
ButtonClickResult EvalButtonTouch(int cur_state, uint32_t event_type);

#endif
