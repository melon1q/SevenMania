#![deny(unsafe_op_in_unsafe_fn)]

use std::cmp::Ordering;
use std::collections::{HashMap, HashSet};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr;
use std::slice;

const ABI_VERSION: u32 = 2;
const OK: u32 = 0;
const INVALID_ARGUMENT: u32 = 1;
const INVALID_UTF8: u32 = 2;
const DUPLICATE_CHART_ID: u32 = 3;
const INDEX_OUT_OF_RANGE: u32 = 4;
const BUFFER_TOO_SMALL: u32 = 5;
const LENGTH_OVERFLOW: u32 = 6;
const PANIC: u32 = 255;

const MODE_OTHER: u32 = 6;
const TEXT_PATH: u32 = 1;
const TEXT_TITLE: u32 = 2;
const TEXT_SUBTITLE: u32 = 3;
const TEXT_ARTIST: u32 = 4;
const TEXT_PLAY_LEVEL: u32 = 5;
const TEXT_DIRECTORY: u32 = 6;
const GROUP_TEXT_TITLE: u32 = 1;
const GROUP_TEXT_ARTIST: u32 = 2;
const GROUP_TEXT_DIRECTORY: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BmsSongSelectionUtf8Slice {
    pub data: *const u8,
    pub length_bytes: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BmsSongChartSummaryInput {
    pub chart_id: u64,
    pub path: BmsSongSelectionUtf8Slice,
    pub title: BmsSongSelectionUtf8Slice,
    pub subtitle: BmsSongSelectionUtf8Slice,
    pub artist: BmsSongSelectionUtf8Slice,
    pub play_level: BmsSongSelectionUtf8Slice,
    pub directory: BmsSongSelectionUtf8Slice,
    pub mode: u32,
    pub playable: u32,
    pub title_group: u32,
    pub subtitle_group: u32,
    pub has_numeric_play_level: u32,
    pub numeric_play_level: f64,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct BmsSongChartSummaryInfo {
    pub chart_id: u64,
    pub mode: u32,
    pub playable: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct BmsSongSelectionModelInfo {
    pub generation: u64,
    pub sort_descending: u32,
    pub group_count: u32,
    pub selected_group_index: i32,
    pub selected_chart_index: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct BmsSongSelectionGroupInfo {
    pub chart_count: u32,
    pub selected_variant: u32,
    pub mode: u32,
}

struct ChartSummary {
    chart_id: u64,
    path: String,
    title: String,
    subtitle: String,
    artist: String,
    play_level: String,
    directory: String,
    mode: u32,
    playable: u32,
    title_group: u32,
    subtitle_group: u32,
    numeric_play_level: Option<f64>,
}

/// Opaque to C++; all owned strings are copied into this Rust allocation.
pub struct BmsSongSelectionSnapshot {
    generation: u64,
    charts: Vec<ChartSummary>,
}

struct SelectionGroup {
    title: String,
    artist: String,
    directory: String,
    title_group: u32,
    mode: u32,
    chart_indices: Vec<u32>,
    selected_variant: usize,
}

/// Rust owns the selection state and chart order. C++ receives value snapshots for display.
pub struct BmsSongSelectionModel {
    snapshot: BmsSongSelectionSnapshot,
    groups: Vec<SelectionGroup>,
    selected_directory: Option<String>,
    sort_descending: bool,
}

#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn CompareStringOrdinal(
        string1: *const u16,
        count1: i32,
        string2: *const u16,
        count2: i32,
        ignore_case: i32,
    ) -> i32;
    fn CompareStringEx(
        locale_name: *const u16,
        flags: u32,
        string1: *const u16,
        count1: i32,
        string2: *const u16,
        count2: i32,
        version_info: *mut std::ffi::c_void,
        reserved: *mut std::ffi::c_void,
        sort_handle: isize,
    ) -> i32;
}

#[cfg(windows)]
fn windows_compare(left: &str, right: &str, group: u32) -> Option<Ordering> {
    let left: Vec<u16> = left.encode_utf16().collect();
    let right: Vec<u16> = right.encode_utf16().collect();
    let left_len = i32::try_from(left.len()).ok()?;
    let right_len = i32::try_from(right.len()).ok()?;
    let result = unsafe {
        if group == 0 {
            CompareStringOrdinal(left.as_ptr(), left_len, right.as_ptr(), right_len, 1)
        } else {
            let locale: &[u16] = if group == 1 {
                &[
                    b'k' as u16,
                    b'o' as u16,
                    b'-' as u16,
                    b'K' as u16,
                    b'R' as u16,
                    0,
                ]
            } else {
                &[
                    b'j' as u16,
                    b'a' as u16,
                    b'-' as u16,
                    b'J' as u16,
                    b'P' as u16,
                    0,
                ]
            };
            CompareStringEx(
                locale.as_ptr(),
                1, // NORM_IGNORECASE
                left.as_ptr(),
                left_len,
                right.as_ptr(),
                right_len,
                ptr::null_mut(),
                ptr::null_mut(),
                0,
            )
        }
    };
    match result {
        1 => Some(Ordering::Less),
        2 => Some(Ordering::Equal),
        3 => Some(Ordering::Greater),
        _ => None,
    }
}

fn compare_text(left: &str, right: &str, group: u32) -> Ordering {
    #[cfg(windows)]
    if let Some(order) = windows_compare(left, right, group) {
        return order;
    }

    left.to_lowercase()
        .encode_utf16()
        .cmp(right.to_lowercase().encode_utf16())
}

fn guarded(operation: impl FnOnce() -> u32) -> u32 {
    catch_unwind(AssertUnwindSafe(operation)).unwrap_or(PANIC)
}

fn copy_utf8(input: BmsSongSelectionUtf8Slice) -> Result<String, u32> {
    if input.length_bytes == 0 {
        return Ok(String::new());
    }
    if input.data.is_null() {
        return Err(INVALID_ARGUMENT);
    }

    // The C ABI contract requires this non-null pointer to reference
    // length_bytes readable bytes for the duration of this call.
    let bytes = unsafe { slice::from_raw_parts(input.data, input.length_bytes as usize) };
    let value = std::str::from_utf8(bytes).map_err(|_| INVALID_UTF8)?;
    Ok(value.to_owned())
}

unsafe fn copy_chart_summaries(
    charts: *const BmsSongChartSummaryInput,
    chart_count: u32,
) -> Result<Vec<ChartSummary>, u32> {
    let entries = if chart_count == 0 {
        &[]
    } else {
        if charts.is_null() {
            return Err(INVALID_ARGUMENT);
        }
        // The caller must provide chart_count readable, properly aligned entries.
        unsafe { slice::from_raw_parts(charts, chart_count as usize) }
    };

    let mut seen_ids = HashSet::with_capacity(entries.len());
    let mut copied_charts = Vec::with_capacity(entries.len());
    for entry in entries {
        if entry.chart_id == 0
            || entry.mode > MODE_OTHER
            || entry.playable > 1
            || entry.title_group > 2
            || entry.subtitle_group > 2
            || entry.has_numeric_play_level > 1
            || !entry.numeric_play_level.is_finite()
        {
            return Err(INVALID_ARGUMENT);
        }
        if !seen_ids.insert(entry.chart_id) {
            return Err(DUPLICATE_CHART_ID);
        }

        let path = match copy_utf8(entry.path) {
            Ok(value) => value,
            Err(status) => return Err(status),
        };
        let title = match copy_utf8(entry.title) {
            Ok(value) => value,
            Err(status) => return Err(status),
        };
        let subtitle = match copy_utf8(entry.subtitle) {
            Ok(value) => value,
            Err(status) => return Err(status),
        };
        let artist = match copy_utf8(entry.artist) {
            Ok(value) => value,
            Err(status) => return Err(status),
        };
        let play_level = match copy_utf8(entry.play_level) {
            Ok(value) => value,
            Err(status) => return Err(status),
        };
        let directory = match copy_utf8(entry.directory) {
            Ok(value) => value,
            Err(status) => return Err(status),
        };

        copied_charts.push(ChartSummary {
            chart_id: entry.chart_id,
            path,
            title,
            subtitle,
            artist,
            play_level,
            directory,
            mode: entry.mode,
            playable: entry.playable,
            title_group: entry.title_group,
            subtitle_group: entry.subtitle_group,
            numeric_play_level: (entry.has_numeric_play_level == 1)
                .then_some(entry.numeric_play_level),
        });
    }

    Ok(copied_charts)
}

unsafe fn create_snapshot(
    generation: u64,
    charts: *const BmsSongChartSummaryInput,
    chart_count: u32,
    out_snapshot: *mut *mut BmsSongSelectionSnapshot,
) -> u32 {
    if generation == 0 || out_snapshot.is_null() {
        return INVALID_ARGUMENT;
    }
    let copied_charts = match unsafe { copy_chart_summaries(charts, chart_count) } {
        Ok(value) => value,
        Err(status) => return status,
    };
    let snapshot = Box::new(BmsSongSelectionSnapshot {
        generation,
        charts: copied_charts,
    });
    unsafe { out_snapshot.write(Box::into_raw(snapshot)) };
    OK
}

fn compare_play_level(left: Option<f64>, right: Option<f64>) -> Ordering {
    match (left, right) {
        (Some(left), Some(right)) => left.partial_cmp(&right).unwrap_or(Ordering::Equal),
        (Some(_), None) => Ordering::Less,
        (None, Some(_)) => Ordering::Greater,
        (None, None) => Ordering::Equal,
    }
}

fn build_groups(charts: &[ChartSummary], descending: bool) -> Vec<SelectionGroup> {
    let mut groups = Vec::<SelectionGroup>::new();
    let mut group_by_directory = HashMap::<String, usize>::with_capacity(charts.len());

    for (chart_index, chart) in charts.iter().enumerate() {
        let next_index = groups.len();
        let group_index = *group_by_directory
            .entry(chart.directory.clone())
            .or_insert_with(|| {
                groups.push(SelectionGroup {
                    title: chart.title.clone(),
                    artist: chart.artist.clone(),
                    directory: chart.directory.clone(),
                    title_group: chart.title_group,
                    mode: chart.mode,
                    chart_indices: Vec::new(),
                    selected_variant: 0,
                });
                next_index
            });
        groups[group_index].chart_indices.push(chart_index as u32);
    }

    for group in &mut groups {
        group.chart_indices.sort_by(|left_index, right_index| {
            let left = &charts[*left_index as usize];
            let right = &charts[*right_index as usize];
            right
                .playable
                .cmp(&left.playable)
                .then_with(|| compare_play_level(left.numeric_play_level, right.numeric_play_level))
                .then_with(|| compare_text(&left.subtitle, &right.subtitle, left.subtitle_group))
                .then_with(|| compare_text(&left.path, &right.path, 2))
        });
    }

    groups.sort_by(|left, right| {
        let order = left
            .title_group
            .cmp(&right.title_group)
            .then_with(|| compare_text(&left.title, &right.title, left.title_group))
            .then_with(|| compare_text(&left.artist, &right.artist, 2))
            .then_with(|| left.mode.cmp(&right.mode))
            .then_with(|| compare_text(&left.directory, &right.directory, 2));
        if descending { order.reverse() } else { order }
    });
    groups
}

fn selected_group_index(model: &BmsSongSelectionModel) -> Option<usize> {
    let directory = model.selected_directory.as_ref()?;
    if directory.is_empty() {
        return None;
    }
    model
        .groups
        .iter()
        .position(|group| group.directory == *directory)
}

fn selected_chart_index(model: &BmsSongSelectionModel) -> Option<u32> {
    let group = model.groups.get(selected_group_index(model)?)?;
    group.chart_indices.get(group.selected_variant).copied()
}

fn model_info(model: &BmsSongSelectionModel) -> Result<BmsSongSelectionModelInfo, u32> {
    Ok(BmsSongSelectionModelInfo {
        generation: model.snapshot.generation,
        sort_descending: u32::from(model.sort_descending),
        group_count: u32::try_from(model.groups.len()).map_err(|_| LENGTH_OVERFLOW)?,
        selected_group_index: selected_group_index(model)
            .map(|index| i32::try_from(index).unwrap_or(i32::MAX))
            .unwrap_or(-1),
        selected_chart_index: selected_chart_index(model)
            .map(|index| i32::try_from(index).unwrap_or(i32::MAX))
            .unwrap_or(-1),
    })
}

fn clear_model(model: &mut BmsSongSelectionModel) {
    model.snapshot = BmsSongSelectionSnapshot {
        generation: 0,
        charts: Vec::new(),
    };
    model.groups.clear();
    model.selected_directory = None;
}

fn model_text_field(group: &SelectionGroup, field: u32) -> Result<&str, u32> {
    match field {
        GROUP_TEXT_TITLE => Ok(&group.title),
        GROUP_TEXT_ARTIST => Ok(&group.artist),
        GROUP_TEXT_DIRECTORY => Ok(&group.directory),
        _ => Err(INVALID_ARGUMENT),
    }
}

fn chart_at(snapshot: &BmsSongSelectionSnapshot, chart_index: u32) -> Result<&ChartSummary, u32> {
    snapshot
        .charts
        .get(chart_index as usize)
        .ok_or(INDEX_OUT_OF_RANGE)
}

fn text_field(chart: &ChartSummary, field: u32) -> Result<&str, u32> {
    match field {
        TEXT_PATH => Ok(&chart.path),
        TEXT_TITLE => Ok(&chart.title),
        TEXT_SUBTITLE => Ok(&chart.subtitle),
        TEXT_ARTIST => Ok(&chart.artist),
        TEXT_PLAY_LEVEL => Ok(&chart.play_level),
        TEXT_DIRECTORY => Ok(&chart.directory),
        _ => Err(INVALID_ARGUMENT),
    }
}

/// Public C ABI contract version. Incompatible contract changes must increment it.
#[unsafe(no_mangle)]
pub extern "C" fn bms_song_selection_abi_version() -> u32 {
    ABI_VERSION
}

/// Phase-one build probe; the snapshot API is the public phase-two contract.
#[unsafe(no_mangle)]
pub extern "C" fn bms_song_selection_probe(value: u32) -> u32 {
    value.wrapping_mul(31).wrapping_add(7)
}

/// # Safety
/// Output storage and, when non-empty, all input arrays/slices must be valid
/// according to the documented C ABI pointer and lifetime contract.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_snapshot_create(
    generation: u64,
    charts: *const BmsSongChartSummaryInput,
    chart_count: u32,
    out_snapshot: *mut *mut BmsSongSelectionSnapshot,
) -> u32 {
    if out_snapshot.is_null() {
        return INVALID_ARGUMENT;
    }
    unsafe { out_snapshot.write(ptr::null_mut()) };
    guarded(|| unsafe { create_snapshot(generation, charts, chart_count, out_snapshot) })
}

/// # Safety
/// A non-null snapshot must be a live handle returned by `snapshot_create`
/// and must not be used or destroyed concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_snapshot_destroy(
    snapshot: *mut BmsSongSelectionSnapshot,
) {
    if snapshot.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| {
        // The caller must pass a live handle created by this library exactly once.
        drop(unsafe { Box::from_raw(snapshot) });
    }));
}

/// # Safety
/// The handle must be live; both output pointers must reference writable storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_snapshot_get_metadata(
    snapshot: *const BmsSongSelectionSnapshot,
    out_generation: *mut u64,
    out_chart_count: *mut u32,
) -> u32 {
    if snapshot.is_null() || out_generation.is_null() || out_chart_count.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        let snapshot = unsafe { &*snapshot };
        let count = match u32::try_from(snapshot.charts.len()) {
            Ok(value) => value,
            Err(_) => return LENGTH_OVERFLOW,
        };
        unsafe {
            out_generation.write(snapshot.generation);
            out_chart_count.write(count);
        }
        OK
    })
}

/// # Safety
/// The handle must be live and `out_info` must reference writable storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_snapshot_get_chart(
    snapshot: *const BmsSongSelectionSnapshot,
    chart_index: u32,
    out_info: *mut BmsSongChartSummaryInfo,
) -> u32 {
    if snapshot.is_null() || out_info.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        let snapshot = unsafe { &*snapshot };
        let chart = match chart_at(snapshot, chart_index) {
            Ok(value) => value,
            Err(status) => return status,
        };
        unsafe {
            out_info.write(BmsSongChartSummaryInfo {
                chart_id: chart.chart_id,
                mode: chart.mode,
                playable: chart.playable,
            })
        };
        OK
    })
}

/// # Safety
/// The handle must be live, output_required must be writable, and a non-null
/// output buffer must reference output_capacity_bytes writable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_snapshot_copy_text(
    snapshot: *const BmsSongSelectionSnapshot,
    chart_index: u32,
    field: u32,
    output: *mut u8,
    output_capacity_bytes: u32,
    out_required_bytes: *mut u32,
) -> u32 {
    if out_required_bytes.is_null() || (output_capacity_bytes > 0 && output.is_null()) {
        return INVALID_ARGUMENT;
    }
    unsafe { out_required_bytes.write(0) };
    if snapshot.is_null() {
        return INVALID_ARGUMENT;
    }

    guarded(|| {
        let snapshot = unsafe { &*snapshot };
        let chart = match chart_at(snapshot, chart_index) {
            Ok(value) => value,
            Err(status) => return status,
        };
        let value = match text_field(chart, field) {
            Ok(value) => value.as_bytes(),
            Err(status) => return status,
        };
        let required = match u32::try_from(value.len()) {
            Ok(value) => value,
            Err(_) => return LENGTH_OVERFLOW,
        };
        unsafe { out_required_bytes.write(required) };
        if output_capacity_bytes < required {
            return BUFFER_TOO_SMALL;
        }
        if required > 0 {
            // The caller guarantees that the output buffer is writable for its capacity.
            unsafe { ptr::copy_nonoverlapping(value.as_ptr(), output, required as usize) };
        }
        OK
    })
}

/// # Safety
/// `out_model` must point to aligned, writable storage that does not overlap
/// any live Rust model.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_create(
    out_model: *mut *mut BmsSongSelectionModel,
) -> u32 {
    if out_model.is_null() {
        return INVALID_ARGUMENT;
    }
    unsafe { out_model.write(ptr::null_mut()) };
    guarded(|| {
        let model = Box::new(BmsSongSelectionModel {
            snapshot: BmsSongSelectionSnapshot {
                generation: 0,
                charts: Vec::new(),
            },
            groups: Vec::new(),
            selected_directory: None,
            sort_descending: false,
        });
        unsafe { out_model.write(Box::into_raw(model)) };
        OK
    })
}

/// # Safety
/// A non-null model must be a live handle returned by `model_create`, and this
/// handle must not be used concurrently or destroyed more than once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_destroy(model: *mut BmsSongSelectionModel) {
    if model.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| {
        drop(unsafe { Box::from_raw(model) });
    }));
}

/// # Safety
/// The model must be live. The input array and slices must meet the ABI pointer,
/// alignment, lifetime, immutability, and non-overlap contract.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_rebuild(
    model: *mut BmsSongSelectionModel,
    generation: u64,
    charts: *const BmsSongChartSummaryInput,
    chart_count: u32,
) -> u32 {
    if model.is_null() || generation == 0 {
        return INVALID_ARGUMENT;
    }
    if chart_count > i32::MAX as u32 {
        return LENGTH_OVERFLOW;
    }
    guarded(|| {
        let copied_charts = match unsafe { copy_chart_summaries(charts, chart_count) } {
            Ok(value) => value,
            Err(status) => return status,
        };
        let model = unsafe { &mut *model };
        let snapshot = BmsSongSelectionSnapshot {
            generation,
            charts: copied_charts,
        };
        let groups = build_groups(&snapshot.charts, model.sort_descending);
        model.snapshot = snapshot;
        model.groups = groups;
        model.selected_directory = None;
        OK
    })
}

/// # Safety
/// The model must be a live handle not used concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_clear(model: *mut BmsSongSelectionModel) -> u32 {
    if model.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        clear_model(unsafe { &mut *model });
        OK
    })
}

/// # Safety
/// The model must be a live handle not used concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_sort(model: *mut BmsSongSelectionModel) -> u32 {
    if model.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        let model = unsafe { &mut *model };
        model.groups.sort_by(|left, right| {
            let order = left
                .title_group
                .cmp(&right.title_group)
                .then_with(|| compare_text(&left.title, &right.title, left.title_group))
                .then_with(|| compare_text(&left.artist, &right.artist, 2))
                .then_with(|| left.mode.cmp(&right.mode))
                .then_with(|| compare_text(&left.directory, &right.directory, 2));
            if model.sort_descending {
                order.reverse()
            } else {
                order
            }
        });
        OK
    })
}

/// # Safety
/// The model must be a live handle not used concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_toggle_sort(
    model: *mut BmsSongSelectionModel,
) -> u32 {
    if model.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        let model = unsafe { &mut *model };
        model.sort_descending = !model.sort_descending;
        model.groups.sort_by(|left, right| {
            let order = left
                .title_group
                .cmp(&right.title_group)
                .then_with(|| compare_text(&left.title, &right.title, left.title_group))
                .then_with(|| compare_text(&left.artist, &right.artist, 2))
                .then_with(|| left.mode.cmp(&right.mode))
                .then_with(|| compare_text(&left.directory, &right.directory, 2));
            if model.sort_descending {
                order.reverse()
            } else {
                order
            }
        });
        OK
    })
}

/// # Safety
/// The model must be live and `out_info` must be aligned writable storage
/// separate from the model allocation.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_get_info(
    model: *const BmsSongSelectionModel,
    out_info: *mut BmsSongSelectionModelInfo,
) -> u32 {
    if model.is_null() || out_info.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        let info = match model_info(unsafe { &*model }) {
            Ok(value) => value,
            Err(status) => return status,
        };
        unsafe { out_info.write(info) };
        OK
    })
}

/// # Safety
/// The model must be live and `out_info` must be aligned writable storage
/// separate from the model allocation.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_get_group(
    model: *const BmsSongSelectionModel,
    group_index: u32,
    out_info: *mut BmsSongSelectionGroupInfo,
) -> u32 {
    if model.is_null() || out_info.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        let model = unsafe { &*model };
        let group = match model.groups.get(group_index as usize) {
            Some(value) => value,
            None => return INDEX_OUT_OF_RANGE,
        };
        let info = BmsSongSelectionGroupInfo {
            chart_count: match u32::try_from(group.chart_indices.len()) {
                Ok(value) => value,
                Err(_) => return LENGTH_OVERFLOW,
            },
            selected_variant: match u32::try_from(group.selected_variant) {
                Ok(value) => value,
                Err(_) => return LENGTH_OVERFLOW,
            },
            mode: group.mode,
        };
        unsafe { out_info.write(info) };
        OK
    })
}

/// # Safety
/// The model must be live and `out_chart_index` must be aligned writable
/// storage separate from the model allocation.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_get_group_chart_index(
    model: *const BmsSongSelectionModel,
    group_index: u32,
    variant_index: u32,
    out_chart_index: *mut u32,
) -> u32 {
    if model.is_null() || out_chart_index.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        let model = unsafe { &*model };
        let group = match model.groups.get(group_index as usize) {
            Some(value) => value,
            None => return INDEX_OUT_OF_RANGE,
        };
        let chart_index = match group.chart_indices.get(variant_index as usize) {
            Some(value) => *value,
            None => return INDEX_OUT_OF_RANGE,
        };
        unsafe { out_chart_index.write(chart_index) };
        OK
    })
}

/// # Safety
/// The model must be live and `out_selected` must be aligned writable storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_select_group(
    model: *mut BmsSongSelectionModel,
    group_index: i32,
    out_selected: *mut u32,
) -> u32 {
    if model.is_null() || out_selected.is_null() {
        return INVALID_ARGUMENT;
    }
    unsafe { out_selected.write(0) };
    guarded(|| {
        let model = unsafe { &mut *model };
        let index = match usize::try_from(group_index) {
            Ok(value) => value,
            Err(_) => return OK,
        };
        let group = match model.groups.get(index) {
            Some(value) => value,
            None => return OK,
        };
        model.selected_directory = Some(group.directory.clone());
        unsafe { out_selected.write(1) };
        OK
    })
}

/// # Safety
/// The model must be a live handle not used concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_clear_selection(
    model: *mut BmsSongSelectionModel,
) -> u32 {
    if model.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        unsafe { &mut *model }.selected_directory = None;
        OK
    })
}

/// # Safety
/// The model must be live and `out_changed` must be aligned writable storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_cycle_selected_variant(
    model: *mut BmsSongSelectionModel,
    delta: i32,
    out_changed: *mut u32,
) -> u32 {
    if model.is_null() || out_changed.is_null() {
        return INVALID_ARGUMENT;
    }
    unsafe { out_changed.write(0) };
    guarded(|| {
        let model = unsafe { &mut *model };
        let group_index = match selected_group_index(model) {
            Some(value) => value,
            None => return OK,
        };
        let group = &mut model.groups[group_index];
        let count = group.chart_indices.len();
        if count < 2 {
            return OK;
        }
        let count = count as i64;
        let current = (group.selected_variant as i64).clamp(0, count - 1);
        let next = (current + i64::from(delta)).rem_euclid(count);
        if next == current {
            return OK;
        }
        group.selected_variant = next as usize;
        unsafe { out_changed.write(1) };
        OK
    })
}

/// # Safety
/// The model must be live; `out_required_bytes` must be writable, and a
/// non-null output must point to output_capacity_bytes writable bytes. Output
/// memory must not overlap the model allocation or required-length output.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_song_selection_model_copy_group_text(
    model: *const BmsSongSelectionModel,
    group_index: u32,
    field: u32,
    output: *mut u8,
    output_capacity_bytes: u32,
    out_required_bytes: *mut u32,
) -> u32 {
    if out_required_bytes.is_null() || (output_capacity_bytes > 0 && output.is_null()) {
        return INVALID_ARGUMENT;
    }
    unsafe { out_required_bytes.write(0) };
    if model.is_null() {
        return INVALID_ARGUMENT;
    }
    guarded(|| {
        let model = unsafe { &*model };
        let group = match model.groups.get(group_index as usize) {
            Some(value) => value,
            None => return INDEX_OUT_OF_RANGE,
        };
        let value = match model_text_field(group, field) {
            Ok(value) => value.as_bytes(),
            Err(status) => return status,
        };
        let required = match u32::try_from(value.len()) {
            Ok(value) => value,
            Err(_) => return LENGTH_OVERFLOW,
        };
        unsafe { out_required_bytes.write(required) };
        if output_capacity_bytes < required {
            return BUFFER_TOO_SMALL;
        }
        if required > 0 {
            unsafe { ptr::copy_nonoverlapping(value.as_ptr(), output, required as usize) };
        }
        OK
    })
}

#[cfg(test)]
mod tests {
    use super::{bms_song_selection_abi_version, bms_song_selection_probe};
    use std::mem::{align_of, size_of};

    #[test]
    fn exports_have_stable_probe_results() {
        assert_eq!(bms_song_selection_abi_version(), 2);
        assert_eq!(bms_song_selection_probe(0), 7);
        assert_eq!(bms_song_selection_probe(12), 379);
        assert_eq!(bms_song_selection_probe(u32::MAX), u32::MAX - 23);
        assert_eq!(size_of::<super::BmsSongSelectionUtf8Slice>(), 16);
        assert_eq!(size_of::<super::BmsSongChartSummaryInput>(), 136);
        assert_eq!(size_of::<super::BmsSongChartSummaryInfo>(), 16);
        assert_eq!(size_of::<super::BmsSongSelectionModelInfo>(), 24);
        assert_eq!(size_of::<super::BmsSongSelectionGroupInfo>(), 12);
        assert_eq!(align_of::<super::BmsSongSelectionUtf8Slice>(), 8);
        assert_eq!(align_of::<super::BmsSongChartSummaryInput>(), 8);
        assert_eq!(align_of::<super::BmsSongChartSummaryInfo>(), 8);
        assert_eq!(align_of::<super::BmsSongSelectionModelInfo>(), 8);
        assert_eq!(align_of::<super::BmsSongSelectionGroupInfo>(), 4);
    }
}
