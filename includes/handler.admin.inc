<?php
/**
 * @file
 * Houses Islandora OAI request handler specific configuration.
 */

/**
 * Configuration form for the standard Islandora OAI request handler.
 *
 * @param array $form
 *   An array representing a Drupal form.
 * @param array $form_state
 *   An array containing the Drupal form state.
 *
 * @return array
 *   A renderable array representing the handler configuration form.
 */
function islandora_esdn_oai_handler_configuration_form($form, &$form_state) {
  module_load_include('inc', 'islandora_oai', 'includes/handler.admin');
  $form['islandora_esdn_oai_configuration'] = array(
    '#type' => 'fieldset',
    '#title' => t('Deleted Objects management'),
    '#collapsible' => FALSE,
    '#collapsed' => TRUE,
  );
  
  $form['islandora_esdn_oai_configuration']['islandora_esdn_oai_keep_tombstone'] = array(
    '#type' => 'checkbox',
    '#title' => t('Create Tombstone for purged Objects'),
    '#size' => '50',
    '#default_value' => variable_get('islandora_esdn_oai_keep_tombstone', FALSE),
    '#description' => t('If this option is selected, we will store a minimal data version of any purged object in DB to be able to handle deleted records in OAI'),
  );
  $form['islandora_esdn_oai_configuration']['islandora_esdn_oai_create_tombstone_solr'] = array(
    '#type' => 'checkbox',
    '#title' => t('Create Solr document Tombstone for purged Objects'),
    '#size' => '50',
    '#default_value' => variable_get('islandora_esdn_oai_create_tombstone_solr', FALSE),
    '#description' => t('If this option is selected, we will create minimal data version of any purged object in Solr to be able to handle deleted records in OAI'),
  );
  $form['islandora_esdn_oai_configuration']['islandora_esdn_oai_tombstone_namespace'] = array(
    '#type' => 'textfield',
    '#title' => t('Solr PID namespace to be used as unique ID for tombstone Solr documents'),
    '#size' => '50',
    '#default_value' => variable_get('islandora_esdn_oai_tombstone_namespace', 'deletedobjectsrepublic'),
    '#description' => t('This module creates a new Solr Document for each deleted object with a PID, prefixed with a special namespace and sha1 of the original PID. This option allows you to define the namespace'),
  );
  
  $form = $form + islandora_oai_handler_configuration_form($form, $form_state);
  
  return $form;
}

/**
 * Validation for the handler configuration form.
 *
 * @param array $form
 *   An array representing a Drupal form.
 * @param array $form_state
 *   An array containing the Drupal form state.
 */
function islandora_esdn_oai_handler_configuration_form_validate($form, &$form_state) {
  $metadata_formats = array();
  $results = db_query('SELECT metadata_prefix FROM {islandora_oai_metadata_formats}');
  foreach ($results as $row) {
    $metadata_formats[$row->metadata_prefix] = $row->metadata_prefix;
  }
  foreach ($metadata_formats as $format) {
    if ($form_state['values']['islandora_oai_metadata'][$format]["islandora_oai_include_object_links_for_{$format}"]) {
      $field = trim($form_state['values']['islandora_oai_metadata'][$format]["islandora_oai_object_links_for_{$format}_field"]);
      if (empty($field)) {
        form_error($form['islandora_oai_metadata'][$format]["islandora_oai_object_links_for_{$format}_field"], t('The field must not be empty.'));
      }
    }
  }
}

/**
 * Submit handler for the handler configuration form.
 *
 * @param array $form
 *   An array representing a Drupal form.
 * @param array $form_state
 *   An array containing the Drupal form state.
 */
function islandora_esdn_oai_handler_configuration_form_submit($form, &$form_state) {
  variable_set('islandora_oai_collection_field', $form_state['values']['islandora_oai_configuration']['islandora_oai_collection_field']);
  variable_set('islandora_oai_content_model_field', $form_state['values']['islandora_oai_configuration']['islandora_oai_content_model_field']);
  variable_set('islandora_oai_exclude_content_models', $form_state['values']['islandora_oai_configuration']['islandora_oai_exclude_content_models']);
  variable_set('islandora_oai_date_field', $form_state['values']['islandora_oai_configuration']['islandora_oai_date_field']);
  variable_set('islandora_oai_exclude_islandora_namespace', $form_state['values']['islandora_oai_configuration']['islandora_oai_exclude_islandora_namespace']);
  variable_set('islandora_oai_append_dc_thumbnail', $form_state['values']['islandora_oai_configuration']['islandora_oai_append_dc_thumbnail']);
  
  variable_set('islandora_esdn_oai_keep_tombstone', $form_state['values']['islandora_esdn_oai_configuration']['islandora_esdn_oai_keep_tombstone']);
  variable_set('islandora_esdn_oai_create_tombstone_solr', $form_state['values']['islandora_esdn_oai_configuration']['islandora_esdn_oai_create_tombstone_solr']);
  variable_set('islandora_esdn_oai_tombstone_namespace', $form_state['values']['islandora_esdn_oai_configuration']['islandora_esdn_oai_tombstone_namespace']);
   
  // Loop through our transform options.
  foreach ($form_state['values']['islandora_oai_metadata']['islandora_oai_options'] as $key => $value) {
    variable_set($key, $value);
  }
  // Loop through object linking.
  $metadata_formats = array();
  $results = db_query('SELECT metadata_prefix FROM {islandora_oai_metadata_formats}');
  foreach ($results as $row) {
    $metadata_formats[$row->metadata_prefix] = $row->metadata_prefix;
  }
  foreach ($metadata_formats as $format) {
    foreach ($form_state['values']['islandora_oai_metadata'][$format] as $key => $value) {
      variable_set($key, trim($value));
    }
  }
  drupal_set_message(t('The configuration options have been saved.'));
}
