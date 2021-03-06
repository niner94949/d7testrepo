<?php
/**
 * @file
 * Features hooks for the uuid_fpp features component.
 */

/**
 * Implements hook_features_export_options().
 */
function uuid_fpp_features_export_options() {
  $options = array();

  $query = 'SELECT f.fpid, f.admin_title, f.title, f.bundle, f.uuid
    FROM {fieldable_panels_panes} f ORDER BY f.bundle, f.title ASC';
  $results = db_query($query);
  foreach ($results as $fpp) {
    $bundle_name = ucwords(str_replace('_', ' ', $fpp->bundle));
    $display_title = ($fpp->admin_title) ? $fpp->admin_title : $fpp->uuid;

    $options[$fpp->uuid] = $bundle_name . ' - ' . $display_title;
  }

  return $options;
}

/**
 * Implements hook_features_export().
 */
function uuid_fpp_features_export($data, &$export, $module_name = '') {
  $export['dependencies']['fieldable_panels_panes'] = 'fieldable_panels_panes';
  $export['dependencies']['uuid_features'] = 'uuid_features';

  // Add extra dependencies so we don't lose track of bundles.
  $fpp_modules = _uuid_fp_features_provides_bundle();
  foreach ($fpp_modules as $module) {
    $export['dependencies'][$module] = $module;
  }

  $fpids = entity_get_id_by_uuid('fieldable_panels_pane', $data);
  foreach ($fpids as $uuid => $fpid) {
    // Load the FPP matching the $fpid.
    $query = new EntityFieldQuery();
    $fpp = $query
      // We just want one FPP: the one matching
      // the current $fpid.
      ->entityCondition('entity_type', 'fieldable_panels_pane')
      ->propertyCondition('fpid', $fpid)
      ->range(0, 1)
      ->execute();

    $export['features']['uuid_fpp'][$uuid] = $uuid;
    $pipe['fieldable_panels_pane'][$fpp['fieldable_panels_pane'][$fpid]->bundle] = $fpp['fieldable_panels_pane'][$fpid]->bundle;

    // drupal_alter() normally supports just one byref parameter. Using
    // the __drupal_alter_by_ref key, we can store any additional parameters
    // that need to be altered, and they'll be split out into additional params
    // for the hook_*_alter() implementations.  The hook_alter signature is
    // hook_uuid_fpp_features_export_alter(&$export, &$pipe, $fpp).
    $data = &$export;
    $data['__drupal_alter_by_ref'] = array(&$pipe);
    drupal_alter('uuid_fpp_features_export', $data, $fpp);
  }

  return $pipe;
}

/**
 * Implements hook_features_export_render().
 */
function uuid_fpp_features_export_render($module, $data) {
  $translatables = $code = array();

  $code[] = '  $fpps = array();';
  $code[] = '';
  foreach ($data as $uuid) {
    // @todo reset = TRUE as otherwise references (parent, fields) were
    // destroyed.
    $fpps = entity_uuid_load('fieldable_panels_pane', array($uuid), array(), TRUE);
    if (!count($fpps)) {
      continue;
    }

    $first_fpp = reset($fpps);
    $export = clone $first_fpp;

    // Do not export ids.
    unset($export->vid);
    unset($export->fpid);
    unset($export->vuuid);
    unset($export->current_vid);
    unset($export->changed);
    unset($export->uid);

    uuid_features_file_field_export($export, 'fieldable_panels_pane');

    $json = json_encode($export);
    $export_array = json_decode($json, TRUE);

    $code[] = '  $fpps[] = ' . features_var_export($export_array, '  ') . ';';
  }

  if (!empty($translatables)) {
    $code[] = features_translatables_export($translatables, '  ');
  }

  $code[] = '  return $fpps;';
  $code = implode("\n", $code);
  return array('uuid_features_default_fpps' => $code);
}

/**
 * Implements hook_features_revert().
 */
function uuid_fpp_features_revert($module) {
  uuid_fpp_features_rebuild($module);
}

/**
 * Implements hook_features_rebuild().
 *
 * Rebuilds terms based on UUID from code defaults.
 */
function uuid_fpp_features_rebuild($module) {
  $fpps = features_get_default('uuid_fpp', $module);

  if (!empty($fpps)) {
    // Get info about current FPP types available in system.
    $entity_info = entity_get_info('fieldable_panels_pane');
    $entity_type = 'fieldable_panels_pane';

    // Loop through the export.
    foreach ($fpps as $data) {
      // Double-check that FPP can be created/reverted.
      if (!isset($entity_info['bundles'][$data['bundle']])) {
        drupal_set_message('Bundle not found for fieldable panels pane of type ' . $data['bundle'] . '. Fieldable panels pane was not created/reverted.', 'warning');
      }
      else {
        // If this is an update, there will be a by-UUID matching FPP.
        $existing = entity_get_id_by_uuid('fieldable_panels_pane', array($data['uuid']));
        if (!empty($existing)) {
          $fpp = entity_load_single('fieldable_panels_pane', $existing[$data['uuid']]);
          foreach ($data as $key => $value) {
            $fpp->$key = $value;
          }
        }
        else {
          // Create a new FPP.
          $fpp = entity_create('fieldable_panels_pane', $data);
        }

        drupal_alter('uuid_entity_features_rebuild', $entity_type, $fpp, $data, $module);

        uuid_features_file_field_import($fpp, 'fieldable_panels_pane');

        if (!fieldable_panels_panes_save($fpp)) {
          drupal_set_message('Failed to create ' . $data['bundle'] . ' fieldable panels pane ' . $data['label'], 'error');
        }
      }
    }
    module_invoke_all('uuid_entity_features_rebuild_complete', $entity_type, $fpps, $module);
  }
}

/**
 * Discovers modules which provide FPP bundles for dependency mapping.
 *
 * Same process as module_invoke_all().
 */
function _uuid_fp_features_provides_bundle() {

  $implements = array();
  $entity_info = fieldable_panels_panes_entity_info();

  // Discover all modules which implement entity_info_alter
  foreach (module_implements('entity_info_alter') as $module) {
    $function = $module . '_entity_info_alter';
    if (function_exists($function)) {

      // Essentially we need to invoke the hook_alter() and see if our target
      // entity had some values added.
      $function($entity_info);

      // If we have a key for our entity, log the module.
      if (isset($entity_info['fieldable_panels_pane'])) {
        $implements[] = $module;
      }
    }
  }
  return $implements;
}
